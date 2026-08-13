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

#ifndef MANGOS_COURSETIME_H
#define MANGOS_COURSETIME_H

#include "Platform/Define.h"

/**
 * @brief The millisecond clock a course is written against, and the one arithmetic
 *        rule that keeps it honest.
 *
 * The server's millisecond clock is a 32-bit counter that wraps every 49.7 days, and a
 * course routinely straddles a wrap: the longest leg observed in the retail captures is
 * 143.9 seconds, but a course is COMPARED against `now` for as long as the unit lives.
 * Comparing two wrapped stamps with `<` is the bug that makes a creature freeze forever
 * once a realm has been up for seven weeks, and it is invisible in testing because the
 * window is one instant in 49.7 days.
 *
 * So an Instant is never compared directly. Every question is asked as a SIGNED
 * DIFFERENCE, which is correct across a wrap for any two stamps less than 24.8 days
 * apart -- a bound no movement leg can approach.
 */
namespace Helm
{
    /// A point on the server's millisecond clock. Wraps; never compare with < or >.
    using Instant = uint32;

    /// A span of milliseconds. Always small enough to be exact.
    using Millis = int32;

    /**
     * @brief `a - b`, correct across the 32-bit wrap.
     *
     * Positive when `a` is later than `b`. The cast through uint32 makes the
     * subtraction modular (defined behaviour), and the cast back to int32 reads the
     * result as the shortest signed distance between the two stamps.
     */
    inline Millis Since(Instant a, Instant b)
    {
        return Millis(uint32(a) - uint32(b));
    }

    /// True when `a` is at or after `b` on the wrapping clock.
    inline bool AtOrAfter(Instant a, Instant b)
    {
        return Since(a, b) >= 0;
    }

    /// `t` advanced by `ms`, which may be negative.
    inline Instant Advance(Instant t, Millis ms)
    {
        return Instant(uint32(t) + uint32(ms));
    }
}

#endif // MANGOS_COURSETIME_H
