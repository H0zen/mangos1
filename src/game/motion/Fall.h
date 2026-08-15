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

#ifndef MANGOS_HELM_FALL_H
#define MANGOS_HELM_FALL_H

/**
 * @file Fall.h
 * @brief A FALL IS NOT A TRAVEL, and this is the difference written down.
 *
 * Every other leg in this layer is a polyline walked at a speed: the geometry decides the
 * shape and the speed decides the clock. A fall has neither. Its shape is one vertical
 * line, and its clock is gravity -- so a ten-yard drop takes about one second, where the
 * same ten yards walked take four. Sending a fall as a travel is not an approximation of
 * it; it is a different event with a fabricated duration, and the server then arrives on
 * its own schedule while the client is still drawing a slow diagonal.
 *
 * == Why the constants live here and not in the packet layer ==
 *
 * They were in `movement/util.cpp`, beside the spline. That is the wrong side of the
 * line: WHERE a body is at a given instant is the plan's question, and the plan is not
 * allowed to depend on the layer that packs bytes. Moving them makes a fall expressible
 * as a `Course`, which is what lets the old spline engine be deleted rather than kept
 * alive for this one case.
 *
 * The numbers themselves are the CLIENT's, to the digit. They are not a model chosen
 * here and they must never be tuned: the client computes the elevation itself when
 * FLAG_FALLING is set, so a server that used a different gravity would disagree with the
 * only opinion that matters, continuously, for the whole of every fall.
 */

#include <cmath>

namespace Helm
{
    namespace Fall
    {
        /// Yards per second per second. The client's own figure.
        constexpr float kGravity = 19.29110527038574f;

        /// The speed a long fall settles at, in yards per second. Past this the drop is
        /// linear in time rather than quadratic, which is why `Drop` has two branches.
        constexpr float kTerminalVelocity = 60.148003f;

        /// How far a body has fallen when it reaches terminal velocity.
        constexpr float kTerminalDrop =
            kTerminalVelocity * kTerminalVelocity / (2.0f * kGravity);

        /// How long that takes.
        constexpr float kTerminalTime = kTerminalVelocity / kGravity;

        /// How long a drop of `yards` takes, in seconds.
        inline float Time(float yards)
        {
            if (!(yards > 0.0f))
            {
                return 0.0f;   // NaN takes this branch too; sqrt of one is not a time
            }

            if (yards > kTerminalDrop)
            {
                return (yards - kTerminalDrop) / kTerminalVelocity + kTerminalTime;
            }
            return std::sqrt(2.0f * yards / kGravity);
        }

        /// How far a body has dropped after `seconds`, in yards.
        inline float Drop(float seconds)
        {
            if (!(seconds > 0.0f))
            {
                return 0.0f;
            }

            if (seconds > kTerminalTime)
            {
                return kTerminalVelocity * (seconds - kTerminalTime) + kTerminalDrop;
            }
            return seconds * seconds * kGravity * 0.5f;
        }
    }
}

#endif // MANGOS_HELM_FALL_H
