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

#include "SlotAllocator.h"

namespace aura
{
    std::uint8_t AllocateSlot(bool positive,
                              std::uint32_t const* occupied,
                              std::uint8_t total,
                              std::uint8_t positiveEnd)
    {
        if (!occupied || total == 0)
        {
            return NoSlot;
        }

        // A positive band that runs past the end, or a negative one that
        // starts past it, is a caller who got the two numbers the wrong way
        // round. Refusing is better than scanning off the end of the array.
        if (positiveEnd > total)
        {
            return NoSlot;
        }

        std::uint8_t const first = positive ? 0 : positiveEnd;
        std::uint8_t const last = positive ? positiveEnd : total;

        for (std::uint8_t at = first; at < last; ++at)
        {
            if (occupied[at] == 0)
            {
                return at;
            }
        }

        return NoSlot;
    }
}
