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

#include "SpeedSet.h"

float baseMoveSpeed[MAX_MOVE_TYPE] =
{
    2.5f,                                                   // MOVE_WALK
    7.0f,                                                   // MOVE_RUN
    4.5f,                                                   // MOVE_RUN_BACK
    4.722222f,                                              // MOVE_SWIM
    2.5f,                                                   // MOVE_SWIM_BACK
    3.141594f,                                              // MOVE_TURN_RATE
    7.0f,                                                   // MOVE_FLIGHT
    4.5f,                                                   // MOVE_FLIGHT_BACK
};

float ComposeSpeedRate(UnitMoveType mtype, SpeedFactors const& factors)
{
    const float bonus = factors.nonStackBonus > factors.stackBonus
                      ? factors.nonStackBonus
                      : factors.stackBonus;

    // With no speed aura the bonus stands alone; with one, it scales it.
    float speed = factors.mainMod
                ? bonus * (100.0f + factors.mainMod) / 100.0f
                : bonus;

    if (factors.normalization && IsNormalizableMove(mtype))
    {
        const float ceiling = factors.normalization / baseMoveSpeed[mtype];
        if (speed > ceiling)
        {
            speed = ceiling;
        }
    }

    speed *= factors.stateScale;
    speed *= (100.0f + factors.slow) / 100.0f;
    speed *= factors.creatureRate;

    return speed * factors.ratio;
}

SpeedSet::SpeedSet()
{
    for (int i = 0; i < MAX_MOVE_TYPE; ++i)
    {
        m_rate[i] = 1.0f;
    }
}

bool SpeedSet::Store(UnitMoveType mtype, float rate)
{
    if (rate < 0.0f)
    {
        rate = 0.0f;
    }

    if (m_rate[mtype] == rate)
    {
        return false;
    }

    m_rate[mtype] = rate;
    return true;
}
