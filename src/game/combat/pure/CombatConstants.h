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

#ifndef MANGOS_COMBAT_CONSTANTS_H
#define MANGOS_COMBAT_CONSTANTS_H

#include "CombatTypes.h"

/**
 * @brief Every number the combat rules use, in one place, with its provenance.
 *
 * The old code had these spread over four files as bare literals, which is how
 * a Vanilla glancing cap survived into a 2.4.3 core through several style
 * passes: nobody could see 25 and 40 in the same field of view.
 *
 * Some of these are knowingly wrong for 2.4.3. They keep their current value
 * here and are marked STATE: -- stage 1 must not change behaviour, only move
 * it. CombatCoreTest pins each one, so stage 6 flips a constant and watches a
 * named test go red instead of guessing at a balance change.
 */
namespace Combat
{
    namespace Constants
    {
        // ------------------------------------------------------------------
        // Miss
        // ------------------------------------------------------------------

        /// Base white-swing miss against an equal-level target.
        constexpr Hundredths MISS_BASE = 500;

        /// Dual wield white penalty. Correct for 2.4.3, and correctly not
        /// applied to specials.
        constexpr Hundredths MISS_DUAL_WIELD = 1900;

        /// Against a player victim, miss moves 0.04% per point of skill delta.
        constexpr float MISS_PER_SKILL_PVP = 4.0f;

        /// Against a creature, 0.1% per point while the gap is within ten...
        constexpr float MISS_PER_SKILL_PVE_NEAR = 10.0f;

        /// ...and 0.4% per point beyond it, plus a 2% step. Together these give
        /// the familiar 9% against a +3 target: 500 - ((-15 + 10) * 40 - 200).
        constexpr float MISS_PER_SKILL_PVE_FAR = 40.0f;
        constexpr Hundredths MISS_PVE_FAR_STEP = 200;
        constexpr std::int32_t MISS_PVE_FAR_THRESHOLD = -10;

        constexpr Hundredths MISS_MIN = 0;
        constexpr Hundredths MISS_MAX = 6000;

        // ------------------------------------------------------------------
        // Avoidance
        // ------------------------------------------------------------------

        /// Attacker skill above the victim's maximum reduces dodge, parry and
        /// block by 0.04% per point.
        constexpr Hundredths AVOIDANCE_PER_SKILL = 4;

        /// Creature dodge when nothing else says otherwise.
        constexpr Hundredths CREATURE_DODGE_BASE = 500;

        /// STATE: applied to every creature that lacks the no-parry flag only
        /// after a CREATURE_TYPE_HUMANOID test, so a dragon boss never parried.
        /// The type test is gone -- Caps::canParry now answers it -- but the
        /// magnitude is unchanged.
        constexpr Hundredths CREATURE_PARRY_BASE = 500;

        /// STATE: 5% for any creature without the no-block flag, shield or no
        /// shield. Caps::canBlock is what decides now; the number stays.
        constexpr Hundredths CREATURE_BLOCK_BASE = 500;

        // ------------------------------------------------------------------
        // Crit
        // ------------------------------------------------------------------

        constexpr Hundredths CREATURE_CRIT_BASE = 500;

        /// Crit moves 0.04% per point of the attacker's level skill over the
        /// victim's defence.
        ///
        /// STATE: against a +3 target this is -0.6%. 2.4.3 suppresses raid-boss
        /// crit considerably harder -- between 3% and 4.8% depending on the
        /// source -- and I will not encode a number I cannot cite. Left at the
        /// current value; CombatCoreTest pins -0.6% so the day it is sourced,
        /// one constant and one expectation move together.
        constexpr float CRIT_PER_DEFENCE_POINT = 4.0f;

        /// A crit is double damage before any modifier.
        constexpr std::uint32_t CRIT_MULTIPLIER_NUMERATOR = 2;

        // ------------------------------------------------------------------
        // Glancing
        // ------------------------------------------------------------------

        /// Base glancing chance before the skill gap.
        constexpr Hundredths GLANCE_BASE = 1000;

        /// STATE: VANILLA. 1% per point of (defence - weapon skill), capped at
        /// 25%. 2.4.3 from patch 2.1 is 2% per point capped at 40%, which
        /// against a +3 boss is the difference between 25% and 40% of every
        /// white swing. Flip both in stage 6.
        constexpr Hundredths GLANCE_PER_SKILL_POINT = 100;
        constexpr Hundredths GLANCE_CAP = 2500;

        /// The damage curve. lowEnd = base - 0.05 per point of skill gap,
        /// highEnd = base - 0.03. Against a +3 target this lands at roughly
        /// 55%-75%, which is right for 2.4.3.
        constexpr float GLANCE_LOW_BASE      = 1.3f;
        constexpr float GLANCE_HIGH_BASE     = 1.2f;
        constexpr float GLANCE_LOW_PER_SKILL  = 0.05f;
        constexpr float GLANCE_HIGH_PER_SKILL = 0.03f;

        constexpr float GLANCE_LOW_FLOOR   = 0.01f;
        constexpr float GLANCE_HIGH_FLOOR  = 0.20f;
        constexpr float GLANCE_HIGH_CEIL   = 0.99f;

        /// STATE: INVENTED. The old code subtracted 0.7 from the low end and
        /// 0.3 from the high end when the attacker was a shaman, priest, mage,
        /// warlock or druid, and used a 0.91 low-end ceiling for warriors and
        /// rogues against 0.6 for everyone else. 2.4.3 uses one curve for every
        /// class. The class fork is not represented in this core at all -- it
        /// has nowhere to live, since Profile does not carry a class -- so the
        /// single ceiling below is already the 2.4.3 behaviour.
        ///
        /// This is the one place stage 1 is not bit-identical to the old path,
        /// and it is deliberate: reproducing it would mean inventing a field to
        /// hold an invention. Noted in COMBAT_PLAN.md.
        constexpr float GLANCE_LOW_CEIL = 0.91f;

        // ------------------------------------------------------------------
        // Crushing
        // ------------------------------------------------------------------

        /// Fifteen points of skill deficit opens crushing blows...
        constexpr std::int32_t CRUSH_SKILL_THRESHOLD = 15;

        /// ...at 15%, rising 2% per further point. Correct for 2.4.3.
        constexpr Hundredths CRUSH_PER_SKILL_POINT = 200;
        constexpr Hundredths CRUSH_BASE_OFFSET     = 1500;

        /// 150% damage.
        constexpr std::uint32_t CRUSH_NUMERATOR   = 3;
        constexpr std::uint32_t CRUSH_DENOMINATOR = 2;

        // ------------------------------------------------------------------
        // Armour
        // ------------------------------------------------------------------

        /// reduction = armour / (armour + 400 + 85 * levelMod), levelMod being
        /// the attacker level, steepened above 59. The equation is 2.4.3; what
        /// was wrong was where it was applied -- on every school, and before
        /// the outcome multiplier. StrikeResolver fixes the placement.
        constexpr float ARMOUR_CONSTANT       = 400.0f;
        constexpr float ARMOUR_LEVEL_FACTOR   = 85.0f;
        constexpr float ARMOUR_STEEP_LEVEL    = 59.0f;
        constexpr float ARMOUR_STEEP_SLOPE    = 4.5f;
        constexpr float ARMOUR_MAX_REDUCTION  = 0.75f;

        // ------------------------------------------------------------------
        // Rolling
        // ------------------------------------------------------------------

        /// The roll is uniform over [0, HUNDRED_PERCENT), which is 10000
        /// values.
        ///
        /// The old path used urand(0, 10000) -- inclusive, so 10001 values and
        /// a roll that could exceed any table. The bias is one part in ten
        /// thousand and cannot be measured in play, but the half-open range is
        /// what makes "the bands sum to exactly the table" a testable
        /// statement, so the core takes it. Divergence is intentional.
        constexpr Hundredths ROLL_RANGE = HUNDRED_PERCENT;

        /// A connecting swing never lands for nothing.
        constexpr std::uint32_t MINIMUM_APPLIED_DAMAGE = 1;
    }
}

#endif
