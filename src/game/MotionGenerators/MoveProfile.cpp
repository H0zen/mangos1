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
    // A yard of water costs a yard of ground times how much longer it takes to cross
    // it: base run is 7.0 yd/s against base swim 4.722 yd/s, so about 1.48. Rounded to
    // 1.5, because the ratio is not exact for every creature and the search does not
    // need it to be -- what it needs is for water to stop being free.
    const float PATH_COST_SWIM = 1.5f;

    // Magma and slime stay REACHABLE and merely expensive. Creatures take no
    // environmental damage and have always been cleared to swim in both, so refusing
    // them here would strand anything whose target genuinely sits in lava -- a fire
    // elemental pulled into its own pool. At 5.0 a detour wins until it is five times
    // longer, and where no detour exists the path is still found.
    const float PATH_COST_HAZARD = 5.0f;

    /**
     * @brief The area the mover is standing in.
     *
     * Not "what liquid is here" for its own sake: this is what lets a creature that was
     * shoved into liquid it normally refuses -- knocked into a lake, dropped into slime
     * -- still route its way out of it. Without it the mover's permissions match nothing
     * under its own feet and every query answers "no path" until something else moves it.
     */
    Nav::NavArea AreaUnderfoot(Unit const& mover)
    {
        GridMapLiquidData data;
        mover.GetTerrain()->getLiquidStatus(mover.Where().X(), mover.Where().Y(),
                                            mover.Where().Z(), MAP_ALL_LIQUIDS, &data);

        // TESTED AS BITS, not compared as a value. These constants are flags --
        // 0x01 water, 0x02 ocean, 0x04 magma, 0x08 slime -- and the field routinely
        // carries more than one: dark water sets 0x10 beside the kind, and WMO water
        // sets 0x20. A switch on the whole field matched only the lone-bit cases, so a
        // creature knocked into dark ocean (0x12) was answered "ground", never got the
        // water exemption, and could not route its way back out of the sea.
        const uint32 flags = data.type_flags;

        if (flags & MAP_LIQUID_TYPE_MAGMA)
        {
            return Nav::NavArea::Magma;
        }
        if (flags & MAP_LIQUID_TYPE_SLIME)
        {
            return Nav::NavArea::Slime;
        }
        if (flags & (MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN))
        {
            return Nav::NavArea::Water;
        }

        return Nav::NavArea::Ground;
    }
}

Nav::MoveProfile Nav::ProfileOf(Unit const& mover)
{
    MoveProfile profile;
    profile.allowedAreas = 0;

    // Both are virtual on Unit, so no downcast is needed to ask them.
    profile.canSwim = mover.CanSwim();
    profile.canFly = mover.CanFly();
    profile.ignorePathfinding = mover.hasUnitState(UNIT_STAT_IGNORE_PATHFINDING);

    // What the search charges per surface. Ground is the unit and MUST stay the
    // cheapest: the heuristic is a plain distance in yards, which only underestimates
    // the true remaining cost -- and so only returns the shortest path -- while nothing
    // is crossed for less than one unit per yard. A cost below 1.0 would not make its
    // surface preferred; it would make the search wrong.
    profile.areaCost[uint8_t(NavArea::Ground)] = 1.0f;
    profile.areaCost[uint8_t(NavArea::Shallow)] = 1.0f;
    profile.areaCost[uint8_t(NavArea::Water)] = PATH_COST_SWIM;
    profile.areaCost[uint8_t(NavArea::Magma)] = PATH_COST_HAZARD;
    profile.areaCost[uint8_t(NavArea::Slime)] = PATH_COST_HAZARD;

    if (mover.GetTypeId() == TYPEID_UNIT)
    {
        // CanWalk is the one ability that really is Creature's alone.
        profile.canWalk = static_cast<Creature const&>(mover).CanWalk();
        if (profile.canWalk)
        {
            profile.allowedAreas |= AreaBit(NavArea::Ground) |
                                    AreaBit(NavArea::Shallow);
        }

        // Creatures take no environmental damage, so swimming covers the hazards too.
        if (profile.canSwim)
        {
            profile.allowedAreas |= AreaBit(NavArea::Water) |
                                    AreaBit(NavArea::Shallow) |
                                    AreaBit(NavArea::Magma) | AreaBit(NavArea::Slime);
        }

        profile.mayLeaveMesh = true;
    }
    else if (mover.GetTypeId() == TYPEID_PLAYER)
    {
        // Perfect support is not possible for a client-driven mover; stay safe.
        profile.canWalk = true;
        profile.allowedAreas |= AreaBit(NavArea::Ground) |
                                AreaBit(NavArea::Shallow) | AreaBit(NavArea::Water);
    }

    // RECOMPUTED, where the mask it replaces was only ever widened. The old filter
    // accumulated this bit and never dropped it, which cost nothing while a router
    // lived for one call -- but the driver now keeps one across legs, so a creature
    // that once dipped into a lake stayed cleared to route through water for the rest
    // of the chase. The exemption is meant to last as long as the mover is in there.
    if (mover.IsInWater() || mover.IsUnderWater())
    {
        profile.allowedAreas |= AreaBit(AreaUnderfoot(mover));

        // And the same fact by name, because the mask alone cannot carry it: a
        // creature that CAN swim has the water bit set whether or not it is wet, and
        // whether the skin is ground it may ride is a question about now.
        profile.inWater = true;
    }

    // How wide the mover is. New: the baked data records the room around every cell, so
    // the mover's own size is a term in the QUERY rather than something eroded into the
    // surface once for everybody at bake time. A murloc and a devilsaur now get
    // different answers about the same doorway, off one bake.
    profile.radius = mover.Where().Extent();

    return profile;
}
