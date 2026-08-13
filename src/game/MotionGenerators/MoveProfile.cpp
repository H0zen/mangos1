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

#include "MoveProfile.h"

#include "Creature.h"
#include "GridMap.h"
#include "Unit.h"

namespace
{
    /**
     * @brief The NAV_* area the mover is standing in.
     *
     * Not "what liquid is here" for its own sake: this is what lets a creature that was
     * shoved into liquid it normally refuses -- knocked into a lake, dropped into slime
     * -- still route its way out of it. Without it the mover's include mask matches
     * nothing under its own feet and every query answers "no path" until something
     * else moves it.
     */
    uint16 AreaUnderfoot(Unit const& mover)
    {
        GridMapLiquidData data;
        mover.GetTerrain()->getLiquidStatus(mover.Where().X(), mover.Where().Y(),
                                            mover.Where().Z(), MAP_ALL_LIQUIDS, &data);

        switch (data.type_flags)
        {
            case MAP_LIQUID_TYPE_WATER:
            case MAP_LIQUID_TYPE_OCEAN:
                return NAV_WATER;
            case MAP_LIQUID_TYPE_MAGMA:
                return NAV_MAGMA;
            case MAP_LIQUID_TYPE_SLIME:
                return NAV_SLIME;
            default:
                return NAV_GROUND;
        }
    }
}

Nav::MoveProfile Nav::ProfileOf(Unit const& mover)
{
    MoveProfile profile;

    // Both are virtual on Unit, so no downcast is needed to ask them.
    profile.canSwim = mover.CanSwim();
    profile.canFly = mover.CanFly();
    profile.ignorePathfinding = mover.hasUnitState(UNIT_STAT_IGNORE_PATHFINDING);

    if (mover.GetTypeId() == TYPEID_UNIT)
    {
        // CanWalk is the one ability that really is Creature's alone.
        profile.canWalk = static_cast<Creature const&>(mover).CanWalk();
        if (profile.canWalk)
        {
            profile.includeFlags |= NAV_GROUND;
        }

        // Creatures take no environmental damage, so swimming covers the hazards too.
        if (profile.canSwim)
        {
            profile.includeFlags |= (NAV_WATER | NAV_MAGMA | NAV_SLIME);
        }

        profile.mayLeaveMesh = true;
    }
    else if (mover.GetTypeId() == TYPEID_PLAYER)
    {
        // Perfect support is not possible for a client-driven mover; stay safe.
        profile.canWalk = true;
        profile.includeFlags |= (NAV_GROUND | NAV_WATER);
    }

    // RECOMPUTED, where the mask it replaces was only ever widened. The old filter
    // accumulated this bit and never dropped it, which cost nothing while a PathFinder
    // lived for one call -- but MotionDriver now keeps one across legs, so a creature
    // that once dipped into a lake stayed cleared to route through water for the rest
    // of the chase. The exemption is meant to last as long as the mover is in there.
    if (mover.IsInWater() || mover.IsUnderWater())
    {
        profile.includeFlags |= AreaUnderfoot(mover);
    }

    return profile;
}
