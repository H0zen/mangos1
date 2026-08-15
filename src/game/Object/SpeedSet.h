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

#ifndef MANGOS_H_SPEEDSET
#define MANGOS_H_SPEEDSET

#include <cstdint>

/**
 * @brief How fast a unit moves, and the arithmetic that decides it.
 *
 * Held by value on Unit. Depends on nothing -- not on Unit, not on the world,
 * not on a DBC -- so a mounted druid in a slow trap can be assembled from
 * written-down numbers and checked.
 *
 * The move types live here rather than in Unit.h because they index this and
 * nothing else.
 */

enum UnitMoveType
{
    MOVE_WALK           = 0,
    MOVE_RUN            = 1,
    MOVE_RUN_BACK       = 2,
    MOVE_SWIM           = 3,
    MOVE_SWIM_BACK      = 4,
    MOVE_TURN_RATE      = 5,
    MOVE_FLIGHT         = 6,
    MOVE_FLIGHT_BACK    = 7,
};

#define MAX_MOVE_TYPE     8

/// Yards per second at a rate of 1.0. A rate is always relative to this, and
/// the client holds the same table.
extern float baseMoveSpeed[MAX_MOVE_TYPE];

/// True for the three types that a normalisation aura can cap. The other five
/// ignore it.
inline bool IsNormalizableMove(UnitMoveType mtype)
{
    return mtype == MOVE_RUN || mtype == MOVE_SWIM || mtype == MOVE_FLIGHT;
}

/**
 * @brief Everything one move type's rate is assembled from.
 *
 * The two bonuses do not add: the stacking auras are multiplied together into
 * one and the best non-stacking aura into another, and only the larger of the
 * two survives.
 */
struct SpeedFactors
{
    std::int32_t mainMod       = 0;    ///< percent, the strongest speed aura
    float        stackBonus    = 1.0f; ///< multiplier, the stacking auras
    float        nonStackBonus = 1.0f; ///< multiplier, the best non-stacking
    std::int32_t normalization = 0;    ///< yards/sec ceiling; 0 is no ceiling
    float        stateScale    = 1.0f; ///< searching for help, or being a ghost
    std::int32_t slow          = 0;    ///< percent, negative
    float        creatureRate  = 1.0f; ///< the template's own walk/run rate
    float        ratio         = 1.0f;
};

/**
 * @brief The rate the factors come to.
 *
 * The normalisation ceiling is applied before the slow and the creature rate,
 * so an aura that pins a unit to a fixed speed still leaves it slowable.
 */
float ComposeSpeedRate(UnitMoveType mtype, SpeedFactors const& factors);

/**
 * @brief One rate per move type.
 *
 * A rate is a multiplier over baseMoveSpeed, so every one of them starts at
 * 1.0 and a unit with nothing on it moves at the table's speed.
 */
class SpeedSet
{
    public:
        SpeedSet();

        float Rate(UnitMoveType mtype) const
        {
            return m_rate[mtype];
        }

        /// Yards per second, which is what the client is told.
        float Speed(UnitMoveType mtype) const
        {
            return m_rate[mtype] * baseMoveSpeed[mtype];
        }

        /**
         * @brief Record a new rate, clamping a negative one to standing still.
         *
         * @return false when the rate was already that, in which case nobody
         *         needs to be told.
         */
        bool Store(UnitMoveType mtype, float rate);

    private:
        float m_rate[MAX_MOVE_TYPE];
};

#endif
