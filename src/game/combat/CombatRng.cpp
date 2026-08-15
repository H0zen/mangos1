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

#include "CombatRng.h"

#include "Utilities/Util.h"

namespace Combat
{
    Hundredths WorldRng::Roll10000()
    {
        // urand is inclusive at both ends, so the upper bound is one below a
        // full probability. The old path asked for urand(0, 10000) and got
        // 10001 possible values, one of which no table could contain.
        return static_cast<Hundredths>(urand(0, HUNDRED_PERCENT - 1));
    }

    std::uint32_t WorldRng::RollRange(std::uint32_t low, std::uint32_t high)
    {
        return urand(low, high);
    }

    float WorldRng::RollUnit()
    {
        return rand_norm_f();
    }
}
