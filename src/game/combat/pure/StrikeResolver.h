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

#ifndef MANGOS_COMBAT_STRIKERESOLVER_H
#define MANGOS_COMBAT_STRIKERESOLVER_H

#include "HitTable.h"
#include "Matchup.h"
#include "Rng.h"
#include "Strike.h"

namespace Combat
{
    /**
     * @brief Turns a matchup and a roll into a Strike. No side effects.
     *
     * The old path armoured the raw damage *before* rolling the outcome, then
     * multiplied the armoured number by the crit, glancing or crushing factor.
     * Be precise about what that cost: armour is a multiplier, so swapping it
     * with another multiplier changes the damage by nothing but rounding. Two
     * things did go wrong, and both are fixed here.
     *
     * First, armour was applied to every school, so a creature whose melee
     * lands as fire was armoured instead of resisted. Armour is gated on the
     * school now.
     *
     * Second, and the reason the order matters at all: with mitigation applied
     * before the outcome was known, there was no point in the chain at which
     * "what the victim did not take" could be stated, so the clean-damage
     * bookkeeping that rage and skill-up read was reassembled from subtractions
     * in three different files -- and the attacker's rage on a dodge or a parry
     * came out as zero. Here every stage is a named field on Strike.
     *
     * The order is:
     *
     *   1. roll the outcome from the table
     *   2. roll weapon damage, in integers
     *   3. apply the outcome multiplier (crit, glancing, crushing)
     *   4. apply armour, and only when the school is physical
     *   5. subtract the block, which is a flat value and comes after armour
     *   6. absorb and resist, folded in later by Strike::ApplyAbsorbResist
     */
    class StrikeResolver
    {
        public:
            static Strike Resolve(Matchup const& matchup, HitTable const& table,
                                  DamageRange const& weapon, Rng& rng);

            /**
             * @brief The armour multiplier for a given armour and level.
             *
             * Exposed because it is worth testing on its own, and because the
             * spell path will want the same one rather than its own copy.
             *
             * @return The fraction of damage that survives, in [0.25, 1].
             */
            static float ArmourSurvival(std::uint32_t armor,
                                        std::uint8_t attackerLevel);
    };
}

#endif
