/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "ProfileBuilder.h"

#include "combat/pure/CombatConstants.h"

#include "Creature.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities/MathDefines.h"

#include <algorithm>

namespace Combat
{
    namespace
    {
        /// Hand and WeaponAttackType are the same three values in the same
        /// order, and the arrays on Profile are indexed by both. Keep them
        /// tied here rather than trusting the coincidence at every use.
        static_assert(static_cast<int>(BASE_ATTACK) == 0, "hand order");
        static_assert(static_cast<int>(OFF_ATTACK) == 1, "hand order");
        static_assert(static_cast<int>(RANGED_ATTACK) == 2, "hand order");

        WeaponAttackType AttackTypeOf(Hand hand)
        {
            return static_cast<WeaponAttackType>(Index(hand));
        }

        /// Percent as the old code carried it, into hundredths.
        Hundredths ToHundredths(float percent)
        {
            return static_cast<Hundredths>(percent < 0.0f
                ? percent * 100.0f - 0.5f
                : percent * 100.0f + 0.5f);
        }

        /// Weapon damage is stored as a float and rolled as an integer.
        ///
        /// The old path cast with urand((uint32)min, (uint32)max), so a weapon
        /// with a 0.9 to 1.1 range became urand(0, 1) and dealt nothing half
        /// the time. Rounding to nearest is a behaviour change, small and
        /// deliberate; CombatCoreTest names it.
        std::uint32_t ToDamage(float value)
        {
            return value <= 0.0f
                ? 0u
                : static_cast<std::uint32_t>(value + 0.5f);
        }

        Kind KindOf(Unit const* unit)
        {
            if (unit->GetTypeId() == TYPEID_PLAYER)
            {
                return Kind::Player;
            }

            return static_cast<Creature const*>(unit)->IsPet()
                ? Kind::Pet
                : Kind::Creature;
        }

        std::uint32_t ExtraFlagsOf(Unit const* unit)
        {
            if (unit->GetTypeId() == TYPEID_PLAYER)
            {
                return 0;
            }

            CreatureInfo const* info =
                static_cast<Creature const*>(unit)->GetCreatureInfo();

            return info ? info->ExtraFlags : 0;
        }

        /// The attacker's own crit for a hand, before anything the victim
        /// contributes. The victim's half lands in its own profile.
        Hundredths OwnCrit(Unit const* unit, Hand hand)
        {
            if (unit->GetTypeId() == TYPEID_PLAYER)
            {
                switch (hand)
                {
                    case Hand::Off:
                        return ToHundredths(
                            unit->GetFloatValue(PLAYER_OFFHAND_CRIT_PERCENTAGE));
                    case Hand::Ranged:
                        return ToHundredths(
                            unit->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE));
                    default:
                        return ToHundredths(
                            unit->GetFloatValue(PLAYER_CRIT_PERCENTAGE));
                }
            }

            const float crit =
                static_cast<float>(Constants::CREATURE_CRIT_BASE) / 100.0f +
                static_cast<float>(
                    unit->GetTotalAuraModifier(SPELL_AURA_MOD_CRIT_PERCENT));

            return ToHundredths(crit);
        }

        /// What this unit does to an incoming attacker's miss chance. Positive
        /// makes the attacker miss more.
        Hundredths MissTaken(Unit const* unit, Hand hand)
        {
            const bool ranged = hand == Hand::Ranged;
            float mod = 0.0f;

            if (unit->GetTypeId() == TYPEID_PLAYER)
            {
                Player const* player = static_cast<Player const*>(unit);
                mod += player->GetRatingBonusValue(
                    ranged ? CR_HIT_TAKEN_RANGED : CR_HIT_TAKEN_MELEE);
            }

            mod -= static_cast<float>(unit->GetTotalAuraModifier(ranged
                ? SPELL_AURA_MOD_ATTACKER_RANGED_HIT_CHANCE
                : SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE));

            return ToHundredths(mod);
        }

        /// What this unit does to an incoming attacker's crit chance.
        Hundredths CritTaken(Unit const* unit, Hand hand)
        {
            const bool ranged = hand == Hand::Ranged;

            float mod = static_cast<float>(unit->GetTotalAuraModifier(ranged
                ? SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_CHANCE
                : SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_CHANCE));

            mod += static_cast<float>(unit->GetTotalAuraModifier(
                SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE));

            if (unit->GetTypeId() == TYPEID_PLAYER)
            {
                Player const* player = static_cast<Player const*>(unit);
                mod -= player->GetRatingBonusValue(
                    ranged ? CR_CRIT_TAKEN_RANGED : CR_CRIT_TAKEN_MELEE);
            }

            return ToHundredths(mod);
        }

        /// Resilience, as the share of a crit it removes. Linear in the
        /// rating and capped at a quarter, same as Player's own helper -- read
        /// from the rating rather than probed with a made-up damage value.
        Hundredths CritDamageReduction(Unit const* unit)
        {
            if (unit->GetTypeId() != TYPEID_PLAYER)
            {
                return 0;
            }

            Player const* player = static_cast<Player const*>(unit);
            const float reduction =
                std::min(player->GetRatingBonusValue(CR_CRIT_TAKEN_MELEE) * 2.0f,
                         25.0f);

            return ToHundredths(std::max(reduction, 0.0f));
        }

        /// Whether this unit is able to dodge at all, as opposed to how often.
        ///
        /// The two are separate on purpose. A stunned unit's dodge CHANCE is
        /// zero today only because GetUnitDodgeChance short-circuits on the
        /// stun -- so a cached profile, which is the whole point of this
        /// struct, would keep serving the pre-stun number. The capability is
        /// read here from the same two gates the accessor uses, and the table
        /// consults the capability rather than trusting the magnitude.
        bool CanDodge(Unit const* unit)
        {
            if (unit->hasUnitState(UNIT_STAT_STUNNED))
            {
                return false;
            }

            return unit->GetTypeId() == TYPEID_PLAYER ||
                   !static_cast<Creature const*>(unit)->IsTotem();
        }

        void FillWeapon(Unit const* unit, Profile& profile, Hand hand)
        {
            const std::size_t h = Index(hand);

            switch (hand)
            {
                case Hand::Off:
                    profile.weapon[h].low =
                        ToDamage(unit->GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE));
                    profile.weapon[h].high =
                        ToDamage(unit->GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE));
                    break;

                case Hand::Ranged:
                    profile.weapon[h].low =
                        ToDamage(unit->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE));
                    profile.weapon[h].high =
                        ToDamage(unit->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE));
                    break;

                default:
                    profile.weapon[h].low =
                        ToDamage(unit->GetFloatValue(UNIT_FIELD_MINDAMAGE));
                    profile.weapon[h].high =
                        ToDamage(unit->GetFloatValue(UNIT_FIELD_MAXDAMAGE));
                    break;
            }

            if (profile.weapon[h].high < profile.weapon[h].low)
            {
                profile.weapon[h].high = profile.weapon[h].low;
            }

            // A unit with no damage fields swings for urand(0, 5), not for
            // nothing. Unit::CalculateDamage has done this forever and a large
            // number of creature rows -- and every unarmed punch -- depend on
            // it. The first cut of the resolver treated a zero high end as an
            // empty range and those units stopped hitting entirely.
            if (profile.weapon[h].high == 0)
            {
                profile.weapon[h].high = Constants::WEAPON_FALLBACK_HIGH;
            }
        }
    }

    Profile BuildProfile(Unit const* unit, Unit const* opponent)
    {
        Profile profile;

        if (!unit)
        {
            return profile;
        }

        profile.level = static_cast<std::uint8_t>(unit->getLevel());
        profile.kind  = KindOf(unit);

        profile.maxSkillForLevel =
            static_cast<std::int32_t>(unit->GetMaxSkillValueForLevel(opponent));
        profile.defenseSkill =
            static_cast<std::int32_t>(unit->GetDefenseSkillValue(opponent));

        profile.meleeSchoolMask =
            static_cast<std::uint32_t>(unit->GetMeleeSchoolMask());

        // Defensive numbers. The magnitudes still come from the old accessors
        // -- a flat five percent for any creature that blocks at all, and a
        // parry chance that GetUnitParryChance leaves at zero for anything
        // that is not a humanoid. Stage 6 moves those; moving them here would
        // change balance in a stage that is only supposed to move code.
        profile.dodgeChance = ToHundredths(unit->GetUnitDodgeChance());
        profile.parryChance = ToHundredths(unit->GetUnitParryChance());
        profile.blockChance = ToHundredths(unit->GetUnitBlockChance());
        profile.blockValue  = unit->GetShieldBlockValue();
        profile.armor       = unit->GetArmor();

        profile.critDamageReduction = CritDamageReduction(unit);

        const std::uint32_t extraFlags = ExtraFlagsOf(unit);

        profile.caps.canDodge = CanDodge(unit);
        profile.caps.canParry =
            (extraFlags & CREATURE_FLAG_EXTRA_NO_PARRY) == 0;
        profile.caps.canBlock =
            (extraFlags & CREATURE_FLAG_EXTRA_NO_BLOCK) == 0;
        profile.caps.mayCrush =
            profile.kind == Kind::Creature &&
            (extraFlags & CREATURE_FLAG_EXTRA_NO_CRUSH) == 0;
        profile.caps.mayGlance = profile.kind != Kind::Creature;
        profile.caps.dualWielding = unit->haveOffhandWeapon();

        const std::uint32_t crTypeMask =
            opponent ? opponent->GetCreatureTypeMask() : 0;

        for (std::size_t h = 0; h < HAND_COUNT; ++h)
        {
            const Hand hand = static_cast<Hand>(h);
            const WeaponAttackType attType = AttackTypeOf(hand);

            profile.weaponSkill[h] = static_cast<std::int32_t>(
                unit->GetWeaponSkillValue(attType, opponent));
            profile.speedMs[h] = unit->GetAttackTime(attType);

            profile.critChance[h] = OwnCrit(unit, hand);
            profile.hitChance[h]  = ToHundredths(hand == Hand::Ranged
                ? unit->m_modRangedHitChance
                : unit->m_modMeleeHitChance);

            profile.expertiseReduction[h] =
                unit->GetTypeId() == TYPEID_PLAYER
                    ? ToHundredths(static_cast<Player const*>(unit)
                          ->GetExpertiseDodgeOrParryReduction(attType))
                    : 0;

            profile.dodgeResultMod[h] =
                ToHundredths(static_cast<float>(
                    unit->GetTotalAuraModifierByMiscValue(
                        SPELL_AURA_MOD_COMBAT_RESULT_CHANCE,
                        VICTIMSTATE_DODGE)));

            std::int32_t critDamage = unit->GetTotalAuraModifierByMiscMask(
                SPELL_AURA_MOD_CRIT_DAMAGE_BONUS, SPELL_SCHOOL_MASK_NORMAL);

            if (crTypeMask != 0)
            {
                critDamage += unit->GetTotalAuraModifierByMiscMask(
                    SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, crTypeMask);
            }

            profile.critDamageMod[h] =
                ToHundredths(static_cast<float>(critDamage));

            profile.attackerMissMod[h] = MissTaken(unit, hand);
            profile.attackerCritMod[h] = CritTaken(unit, hand);

            // The victim's half of the crit-damage sum. Melee and ranged are
            // separate auras in 2.4.3 and always were; the old path picked
            // between them on attackType and this picks on the hand.
            profile.attackerCritDamageMod[h] =
                ToHundredths(static_cast<float>(unit->GetTotalAuraModifier(
                    hand == Hand::Ranged
                        ? SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE
                        : SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE)));

            FillWeapon(unit, profile, hand);
        }

        // Zero means "never built", and Engagement treats that as always
        // stale. Nothing stamps a real version yet: the dirty-bit plumbing
        // arrives with the engagement, and until it does a missing stamp
        // costs a rebuild rather than a wrong answer.
        profile.version = 0;

        return profile;
    }

    Situation BuildSituation(Unit const* attacker, Unit const* victim)
    {
        Situation situation;

        if (!attacker || !victim)
        {
            return situation;
        }

        // Where() is the placement component, and it compares inside the
        // shared frame. On a vessel that frame is the vessel's own map, which
        // is the only frame in which two boarded units may be compared at all.
        situation.fromBehind =
            !victim->Where().HasInArc(attacker->Where(), M_PI_F);

        situation.victimSitting =
            victim->GetTypeId() == TYPEID_PLAYER && !victim->IsStandState();

        situation.victimEvading =
            victim->GetTypeId() == TYPEID_UNIT &&
            static_cast<Creature const*>(victim)->IsInEvadeMode();

        return situation;
    }
}
