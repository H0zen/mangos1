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

#ifndef MANGOS_COMBAT_HITTABLE_H
#define MANGOS_COMBAT_HITTABLE_H

#include "CombatTypes.h"
#include "Matchup.h"

#include <array>

namespace Combat
{
    /**
     * @brief The resolved outcome bands for one attacker, victim and hand.
     *
     * A cumulative table in hundredths, indexed by Outcome. Resolving a swing
     * is a scan of at most nine bounds: no branch on unit type, no aura
     * access, no skill arithmetic. All of that happened once, in Matchup.
     *
     * Built by Engagement and kept until either profile's version changes, so
     * a unit standing still hitting the same target rebuilds nothing between
     * swings.
     */
    class HitTable
    {
        public:
            /**
             * @brief The white-swing table: one roll decides everything.
             *
             * Miss, dodge, parry, glancing, block, crit, crushing, normal --
             * in that order, which is the 2.4.3 order.
             */
            static HitTable OneRoll(Matchup const& matchup);

            /**
             * @brief The special-attack table: avoidance only.
             *
             * A yellow attack rolls miss, dodge and parry from a table, then
             * rolls crit and block separately. That is why this table's crit
             * and block bands are deliberately empty and the caller must roll
             * them: a two-roll model has no single band where "blocked and
             * critical" could live, which is exactly why the old one-roll
             * MELEE_HIT_BLOCK_CRIT was unreachable.
             */
            static HitTable TwoRoll(Matchup const& matchup);

            /**
             * @brief Decide the outcome for a roll.
             *
             * @param roll Uniform over [0, HUNDRED_PERCENT).
             */
            Outcome Resolve(Hundredths roll) const;

            /// The upper bound of an outcome's band. For tests and logs.
            Hundredths Bound(Outcome outcome) const;

            /// The width of an outcome's band. Zero means it cannot happen.
            Hundredths Band(Outcome outcome) const;

        private:
            /// Cumulative upper bounds, in Outcome order. Normal is always
            /// HUNDRED_PERCENT, so Resolve always terminates.
            std::array<Hundredths, OUTCOME_COUNT> m_bound{};
    };
}

#endif
