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

#include "Matchup.h"

#include "CombatConstants.h"

#include <algorithm>

namespace Combat
{
    namespace
    {
        /// A player or its pet. The two are treated alike by the glancing and
        /// crushing rules and differently by nothing here.
        bool IsPlayerSide(Kind kind)
        {
            return kind == Kind::Player || kind == Kind::Pet;
        }

        Hundredths RoundToHundredths(float percentUnits)
        {
            return static_cast<Hundredths>(percentUnits < 0.0f
                ? percentUnits - 0.5f
                : percentUnits + 0.5f);
        }

        /// Miss, before the attacker's and victim's own modifiers.
        Hundredths BaseMiss(Profile const& attacker, Profile const& victim,
                            Hand hand, bool special)
        {
            using namespace Constants;

            float miss = static_cast<float>(MISS_BASE);

            // The dual-wield penalty is a white-swing rule: specials use their
            // own table and do not carry it.
            //
            // The old code had to guess whether it was on a special, and did
            // it by walking every current spell slot looking for one with a
            // physical school -- so an auto shot in flight, or any physical
            // cast, silently cancelled the penalty on an ordinary swing. The
            // caller knows; it says so.
            if (attacker.caps.dualWielding && hand != Hand::Ranged && !special)
            {
                miss += static_cast<float>(MISS_DUAL_WIELD);
            }

            const std::int32_t skillDiff =
                attacker.weaponSkill[Index(hand)] - victim.defenseSkill;

            if (victim.kind == Kind::Player)
            {
                miss -= static_cast<float>(skillDiff) * MISS_PER_SKILL_PVP;
            }
            else if (skillDiff < MISS_PVE_FAR_THRESHOLD)
            {
                miss -= static_cast<float>(skillDiff - MISS_PVE_FAR_THRESHOLD) *
                            MISS_PER_SKILL_PVE_FAR -
                        static_cast<float>(MISS_PVE_FAR_STEP);
            }
            else
            {
                miss -= static_cast<float>(skillDiff) * MISS_PER_SKILL_PVE_NEAR;
            }

            return RoundToHundredths(miss);
        }

        /// The glancing damage window. Independent of class: the caster
        /// penalty the old code applied was not a 2.4.3 rule.
        void GlanceWindow(std::int32_t skillGap, float& low, float& high)
        {
            using namespace Constants;

            const float gap = static_cast<float>(skillGap);

            low  = GLANCE_LOW_BASE - GLANCE_LOW_PER_SKILL * gap;
            high = GLANCE_HIGH_BASE - GLANCE_HIGH_PER_SKILL * gap;

            low  = std::min(std::max(low, GLANCE_LOW_FLOOR), GLANCE_LOW_CEIL);
            high = std::min(std::max(high, GLANCE_HIGH_FLOOR), GLANCE_HIGH_CEIL);

            // A window that inverted would make the roll below meaningless.
            low = std::min(low, high);
        }
    }

    Matchup Matchup::Build(Profile const& attacker, Profile const& victim,
                           Hand hand, Situation const& situation, bool special)
    {
        using namespace Constants;

        const std::size_t h = Index(hand);

        Matchup m;
        m.hand          = hand;
        m.attackerKind  = attacker.kind;
        m.victimKind    = victim.kind;
        m.attackerLevel = attacker.level;
        m.schoolMask    = attacker.meleeSchoolMask;
        m.armor         = victim.armor;
        m.blockValue    = victim.blockValue;

        m.critDamageMod       = attacker.critDamageMod[h];
        m.critDamageReduction = victim.critDamageReduction;

        m.evading = situation.victimEvading;
        m.immune  = situation.victimImmune;

        if (m.evading || m.immune)
        {
            return m;
        }

        // Attacker skill over the victim's ceiling for its level. Positive
        // means the victim avoids less; the sign is applied by subtraction
        // below, matching the old path.
        const Hundredths skillBonus = AVOIDANCE_PER_SKILL *
            (attacker.weaponSkill[h] - victim.maxSkillForLevel);

        // -- miss ----------------------------------------------------------

        Hundredths miss = BaseMiss(attacker, victim, hand, special);
        miss -= attacker.hitChance[h];
        miss += victim.attackerMissMod[h];
        m.miss = std::min(std::max(miss, MISS_MIN), MISS_MAX);

        // -- crit ----------------------------------------------------------

        Hundredths crit = attacker.critChance[h];
        crit += victim.attackerCritMod[h];
        crit += RoundToHundredths(
            static_cast<float>(attacker.maxSkillForLevel - victim.defenseSkill) *
            CRIT_PER_DEFENCE_POINT);
        m.crit = std::max(crit, Hundredths(0));

        // A sitting player eats a crit from anything that can crit at all.
        // Nothing between miss and crit is rolled, so the bands stay empty.
        if (victim.kind == Kind::Player && situation.victimSitting &&
            m.crit > 0)
        {
            m.sittingCrit = true;
            return m;
        }

        // -- dodge ---------------------------------------------------------

        // Only players lose their dodge to an attack from behind.
        const bool dodgeDenied =
            victim.kind == Kind::Player && situation.fromBehind;

        if (victim.caps.canDodge && !dodgeDenied)
        {
            Hundredths dodge = victim.dodgeChance;
            dodge -= attacker.expertiseReduction[h];
            dodge += attacker.dodgeResultMod[h];

            // Two gates, in this order: a unit that cannot dodge at all is not
            // given a band by a negative skill bonus.
            if (dodge > 0 && (dodge - skillBonus) > 0)
            {
                m.dodge = dodge - skillBonus;
            }
        }

        // -- parry ---------------------------------------------------------

        if (victim.caps.canParry && !situation.fromBehind)
        {
            Hundredths parry = victim.parryChance;
            parry -= attacker.expertiseReduction[h];

            if (parry > 0 && (parry - skillBonus) > 0)
            {
                m.parry = parry - skillBonus;
            }
        }

        // -- glancing ------------------------------------------------------

        const bool glanceApplies =
            !special &&
            hand != Hand::Ranged &&
            attacker.caps.mayGlance &&
            IsPlayerSide(attacker.kind) &&
            !IsPlayerSide(victim.kind) &&
            attacker.level < victim.level;

        if (glanceApplies)
        {
            // Skill above the level ceiling buys nothing here.
            const std::int32_t skill =
                std::min(attacker.weaponSkill[h], attacker.maxSkillForLevel);
            const std::int32_t gap = victim.defenseSkill - skill;

            const Hundredths chance =
                GLANCE_BASE + gap * GLANCE_PER_SKILL_POINT;
            m.glance = std::min(std::max(chance, Hundredths(0)), GLANCE_CAP);

            GlanceWindow(gap, m.glanceLow, m.glanceHigh);
        }

        // -- block ---------------------------------------------------------

        if (victim.caps.canBlock && !situation.fromBehind)
        {
            const Hundredths block = victim.blockChance;

            if (block > 0 && (block - skillBonus) > 0)
            {
                m.block = block - skillBonus;
            }
        }

        // -- crushing ------------------------------------------------------

        const bool crushApplies =
            !special &&
            attacker.kind == Kind::Creature &&
            attacker.caps.mayCrush;

        if (crushApplies)
        {
            // Defence above the level ceiling does not protect.
            const std::int32_t defense =
                std::min(victim.defenseSkill, victim.maxSkillForLevel);
            const std::int32_t deficit = attacker.maxSkillForLevel - defense;

            if (deficit >= CRUSH_SKILL_THRESHOLD)
            {
                m.crush = deficit * CRUSH_PER_SKILL_POINT - CRUSH_BASE_OFFSET;
            }
        }

        return m;
    }

    Matchup Matchup::Magic(Hundredths resistChance, Situation const& situation,
                           std::uint32_t schoolMask)
    {
        Matchup m;
        m.schoolMask = schoolMask;
        m.evading    = situation.victimEvading;
        m.immune     = situation.victimImmune;

        if (m.evading || m.immune)
        {
            return m;
        }

        m.resist = std::min(std::max(resistChance, Hundredths(0)),
                            HUNDRED_PERCENT);

        return m;
    }
}
