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

#include "StatBlock.h"

void StatBlock::Reset()
{
    for (std::size_t group = 0; group < UNIT_MOD_END; ++group)
    {
        m_values[group][BASE_VALUE]  = 0.0f;
        m_values[group][BASE_PCT]    = 1.0f;
        m_values[group][TOTAL_VALUE] = 0.0f;
        m_values[group][TOTAL_PCT]   = 1.0f;
    }
}

float StatBlock::Value(UnitMods unitMod, UnitModifierType type) const
{
    if (!InRange(unitMod, type))
    {
        return 0.0f;
    }

    if (type == TOTAL_PCT && m_values[unitMod][type] <= 0.0f)
    {
        return 0.0f;
    }

    return m_values[unitMod][type];
}

bool StatBlock::Apply(UnitMods unitMod, UnitModifierType type, float amount,
                      bool apply)
{
    if (!InRange(unitMod, type))
    {
        return false;
    }

    switch (type)
    {
        case BASE_VALUE:
        case TOTAL_VALUE:
            m_values[unitMod][type] += apply ? amount : -amount;
            break;

        case BASE_PCT:
        case TOTAL_PCT:
        {
            // A modifier of -100% would make the multiplier zero, and
            // removing it would then divide by it. Clamping to -200% keeps
            // the round trip finite; the stat is off either way.
            if (amount <= -100.0f)
            {
                amount = -200.0f;
            }

            const float factor = (100.0f + amount) / 100.0f;
            m_values[unitMod][type] *= apply ? factor : (1.0f / factor);
            break;
        }

        default:
            break;
    }

    return true;
}

float StatBlock::Combine(UnitMods unitMod, float createValue) const
{
    if (unitMod >= UNIT_MOD_END)
    {
        return 0.0f;
    }

    if (m_values[unitMod][TOTAL_PCT] <= 0.0f)
    {
        return 0.0f;
    }

    float value = m_values[unitMod][BASE_VALUE] + createValue;
    value *= m_values[unitMod][BASE_PCT];
    value += m_values[unitMod][TOTAL_VALUE];
    value *= m_values[unitMod][TOTAL_PCT];

    return value;
}
