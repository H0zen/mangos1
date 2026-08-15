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

#include "CombatShadow.h"

#include "combat/pure/CombatConstants.h"

#include "Creature.h"
#include "Log.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities/MathDefines.h"
#include "World.h"

#include <atomic>
#include <cstdlib>

namespace Combat
{
    namespace
    {
        /// A band differing by one hundredth of a percent is the two paths
        /// rounding the same float differently, not a rule disagreeing.
        constexpr Hundredths TOLERANCE = 1;

        /// Per field, so a single wrong band cannot bury the others.
        constexpr std::uint32_t REPORTS_PER_FIELD = 20;

        enum Field
        {
            FIELD_MISS = 0,
            FIELD_DODGE,
            FIELD_PARRY,
            FIELD_GLANCE,
            FIELD_BLOCK,
            FIELD_CRIT,
            FIELD_CRUSH,
            FIELD_COUNT
        };

        const char* FieldName(Field field)
        {
            switch (field)
            {
                case FIELD_MISS:   return "miss";
                case FIELD_DODGE:  return "dodge";
                case FIELD_PARRY:  return "parry";
                case FIELD_GLANCE: return "glance";
                case FIELD_BLOCK:  return "block";
                case FIELD_CRIT:   return "crit";
                default:           return "crush";
            }
        }

        std::atomic<std::uint32_t> s_reported[FIELD_COUNT];
        std::atomic<std::uint64_t> s_divergences(0);

        /// The bands RollMeleeOutcomeAgainst would have worked from.
        ///
        /// A transcription, on purpose: it uses the same public accessors in
        /// the same order, so a disagreement is between the RULES and not
        /// between two readings of the world. It rolls nothing and consumes
        /// nothing.
        struct LegacyBands
        {
            Hundredths miss   = 0;
            Hundredths dodge  = 0;
            Hundredths parry  = 0;
            Hundredths glance = 0;
            Hundredths block  = 0;
            Hundredths crit   = 0;
            Hundredths crush  = 0;
        };

        Hundredths ToHundredths(float percent)
        {
            return static_cast<Hundredths>(percent * 100.0f);
        }

        std::uint32_t ExtraFlagsOf(Unit const& unit)
        {
            if (unit.GetTypeId() == TYPEID_PLAYER)
            {
                return 0;
            }

            CreatureInfo const* info =
                static_cast<Creature const&>(unit).GetCreatureInfo();

            return info ? info->ExtraFlags : 0;
        }

        LegacyBands ReadLegacyBands(Unit const& attacker, Unit const& victim,
                                    WeaponAttackType attType)
        {
            LegacyBands bands;

            const std::int32_t attackerMaxSkill =
                attacker.GetMaxSkillValueForLevel(&victim);
            const std::int32_t victimMaxSkill =
                victim.GetMaxSkillValueForLevel(&attacker);
            const std::int32_t attackerWeaponSkill =
                attacker.GetWeaponSkillValue(attType, &victim);
            const std::int32_t victimDefenseSkill =
                victim.GetDefenseSkillValue(&attacker);

            const Hundredths skillBonus =
                4 * (attackerWeaponSkill - victimMaxSkill);

            bands.miss = ToHundredths(
                attacker.MeleeMissChanceCalc(&victim, attType));

            bands.crit = ToHundredths(
                attacker.GetUnitCriticalChance(attType, &victim));
            if (bands.crit < 0)
            {
                bands.crit = 0;
            }

            const bool fromBehind =
                !victim.Where().HasInArc(attacker.Where(), M_PI_F);

            const Hundredths expertise =
                attacker.GetTypeId() == TYPEID_PLAYER
                    ? ToHundredths(static_cast<Player const&>(attacker)
                          .GetExpertiseDodgeOrParryReduction(attType))
                    : 0;

            const std::uint32_t extraFlags = ExtraFlagsOf(victim);

            // dodge
            if (victim.GetTypeId() != TYPEID_PLAYER || !fromBehind)
            {
                Hundredths dodge = ToHundredths(victim.GetUnitDodgeChance());
                dodge -= expertise;
                dodge += attacker.GetTotalAuraModifierByMiscValue(
                    SPELL_AURA_MOD_COMBAT_RESULT_CHANCE, VICTIMSTATE_DODGE) * 100;

                if (dodge > 0 && (dodge - skillBonus) > 0)
                {
                    bands.dodge = dodge - skillBonus;
                }
            }

            // parry
            if (!fromBehind)
            {
                Hundredths parry = ToHundredths(victim.GetUnitParryChance());
                parry -= expertise;

                const bool allowed = victim.GetTypeId() == TYPEID_PLAYER ||
                    (extraFlags & CREATURE_FLAG_EXTRA_NO_PARRY) == 0;

                if (parry > 0 && allowed && (parry - skillBonus) > 0)
                {
                    bands.parry = parry - skillBonus;
                }
            }

            // glancing
            const bool attackerPlayerSide =
                attacker.GetTypeId() == TYPEID_PLAYER ||
                (attacker.GetTypeId() == TYPEID_UNIT &&
                 static_cast<Creature const&>(attacker).IsPet());

            const bool victimPlayerSide =
                victim.GetTypeId() == TYPEID_PLAYER ||
                (victim.GetTypeId() == TYPEID_UNIT &&
                 static_cast<Creature const&>(victim).IsPet());

            if (attType != RANGED_ATTACK && attackerPlayerSide &&
                !victimPlayerSide &&
                attacker.getLevel() < victim.GetLevelForTarget(&attacker))
            {
                const std::int32_t skill =
                    attackerWeaponSkill > attackerMaxSkill
                        ? attackerMaxSkill
                        : attackerWeaponSkill;

                Hundredths glance = (10 + (victimDefenseSkill - skill)) * 100;
                bands.glance = glance > 2500 ? 2500 : glance;
            }

            // block
            if (!fromBehind)
            {
                const bool allowed = victim.GetTypeId() == TYPEID_PLAYER ||
                    (extraFlags & CREATURE_FLAG_EXTRA_NO_BLOCK) == 0;

                if (allowed)
                {
                    const Hundredths block =
                        ToHundredths(victim.GetUnitBlockChance());

                    if (block > 0 && (block - skillBonus) > 0)
                    {
                        bands.block = block - skillBonus;
                    }
                }
            }

            // crushing
            const bool attackerIsPlainCreature =
                attacker.GetTypeId() != TYPEID_PLAYER && !attackerPlayerSide;

            if (attackerIsPlainCreature &&
                (ExtraFlagsOf(attacker) & CREATURE_FLAG_EXTRA_NO_CRUSH) == 0)
            {
                std::int32_t defense = victimDefenseSkill > victimMaxSkill
                    ? victimMaxSkill
                    : victimDefenseSkill;

                const std::int32_t deficit = attackerMaxSkill - defense;
                if (deficit >= 15)
                {
                    bands.crush = deficit * 200 - 1500;
                }
            }

            return bands;
        }

        void Compare(Field field, Hundredths legacy, Hundredths fresh,
                     Unit const& attacker, Unit const& victim, Hand hand)
        {
            if (std::abs(legacy - fresh) <= TOLERANCE)
            {
                return;
            }

            s_divergences.fetch_add(1, std::memory_order_relaxed);

            const std::uint32_t seen =
                s_reported[field].fetch_add(1, std::memory_order_relaxed);

            if (seen >= REPORTS_PER_FIELD)
            {
                return;
            }

            sLog.outError(
                "CombatShadow: %s differs, old %d new %d (hand %u, attacker %s "
                "level %u, victim %s level %u)%s",
                FieldName(field), legacy, fresh,
                static_cast<unsigned>(Index(hand)),
                attacker.GetGuidStr().c_str(), attacker.getLevel(),
                victim.GetGuidStr().c_str(), victim.getLevel(),
                seen + 1 == REPORTS_PER_FIELD ? " [last report for this field]"
                                              : "");
        }
    }

    ShadowMode CurrentShadowMode()
    {
        return static_cast<ShadowMode>(
            sWorld.getConfig(CONFIG_UINT32_COMBAT_SHADOW));
    }

    std::uint64_t ShadowDivergenceCount()
    {
        return s_divergences.load(std::memory_order_relaxed);
    }

    void ShadowMeleeChances(Unit const& attacker, Unit const& victim,
                            Hand hand, Matchup const& fresh)
    {
        // Both short circuits skip the bands entirely on the new side, and the
        // old path expressed them as early returns rather than as numbers.
        // There is nothing to line up.
        if (fresh.evading || fresh.immune || fresh.sittingCrit)
        {
            return;
        }

        const WeaponAttackType attType =
            static_cast<WeaponAttackType>(Index(hand));

        const LegacyBands old = ReadLegacyBands(attacker, victim, attType);

        Compare(FIELD_MISS,   old.miss,   fresh.miss,   attacker, victim, hand);
        Compare(FIELD_DODGE,  old.dodge,  fresh.dodge,  attacker, victim, hand);
        Compare(FIELD_PARRY,  old.parry,  fresh.parry,  attacker, victim, hand);
        Compare(FIELD_BLOCK,  old.block,  fresh.block,  attacker, victim, hand);
        Compare(FIELD_CRIT,   old.crit,   fresh.crit,   attacker, victim, hand);
        Compare(FIELD_CRUSH,  old.crush,  fresh.crush,  attacker, victim, hand);

        // Glancing is one of the five declared divergences -- the new core
        // dropped the caster damage penalty, which is an invention rather than
        // a 2.4.3 rule -- but the CHANCE is untouched, so it is still compared.
        // What is not compared is the damage window.
        Compare(FIELD_GLANCE, old.glance, fresh.glance, attacker, victim, hand);
    }
}
