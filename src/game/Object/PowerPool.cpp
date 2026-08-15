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

#include "PowerPool.h"

std::uint32_t RegeneratedValue(RegenTick const& tick)
{
    const float amount = (tick.base + tick.flatBonus) * tick.percent;

    // The pools are unsigned, so a negative amount has no representation to
    // move to. It moves nothing rather than wrapping to something enormous.
    if (amount <= 0.0f)
    {
        return tick.current;
    }

    const std::uint32_t step = static_cast<std::uint32_t>(amount);

    if (tick.behaviour == PoolBehaviour::Drains)
    {
        return step >= tick.current ? 0 : tick.current - step;
    }

    if (tick.current >= tick.maximum)
    {
        return tick.maximum;
    }

    return tick.maximum - tick.current <= step
         ? tick.maximum
         : tick.current + step;
}
