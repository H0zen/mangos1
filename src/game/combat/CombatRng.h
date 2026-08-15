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

#ifndef MANGOS_COMBAT_COMBATRNG_H
#define MANGOS_COMBAT_COMBATRNG_H

#include "combat/pure/Rng.h"

namespace Combat
{
    /**
     * @brief The server's randomness, wearing the pure core's interface.
     *
     * The whole of the game's use of chance in combat enters here. The core
     * takes an Rng& so a test can hand it a written-down roll; this is what
     * the running server hands it instead.
     */
    class WorldRng : public Rng
    {
        public:
            Hundredths Roll10000() override;

            std::uint32_t RollRange(std::uint32_t low,
                                    std::uint32_t high) override;

            float RollUnit() override;
    };
}

#endif
