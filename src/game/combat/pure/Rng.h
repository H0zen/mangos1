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

#ifndef MANGOS_COMBAT_RNG_H
#define MANGOS_COMBAT_RNG_H

#include "CombatTypes.h"

namespace Combat
{
    /**
     * @brief Where randomness enters the core, and the only place it does.
     *
     * The resolver does not call a global. That is what lets a test hand it
     * the exact roll that sits on a band boundary, and what makes a combat log
     * reproducible from a seed. The virtual dispatch costs a call per swing,
     * against the dozens of aura-list walks it replaces.
     */
    class Rng
    {
        public:
            virtual ~Rng() = default;

            /// Uniform over [0, HUNDRED_PERCENT). Half-open on purpose; see
            /// Constants::ROLL_RANGE.
            virtual Hundredths Roll10000() = 0;

            /// Uniform over [low, high], inclusive, in integers.
            virtual std::uint32_t RollRange(std::uint32_t low,
                                            std::uint32_t high) = 0;

            /// Uniform over [0, 1). Used only by the glancing damage curve.
            virtual float RollUnit() = 0;
    };
}

#endif
