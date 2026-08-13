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

#ifndef MANGOS_MOVEPROFILE_H
#define MANGOS_MOVEPROFILE_H

#include "MoveMapSharedDefines.h"
#include "Platform/Define.h"

class Unit;

namespace Nav
{
    /**
     * @brief What one mover is permitted to do, as a value.
     *
     * The router used to ask the mover these questions itself, mid-route, which is why a
     * geometry component knew what a Creature was and what InhabitType meant. Every such
     * question is answered ONCE here, by ProfileOf, and what the router receives is a
     * struct of decisions it applies without interpreting.
     *
     * That the profile is a value is the point, not a convenience. A permission that is a
     * field can be constructed in a test; a permission that is a call into a live Unit can
     * only be observed by standing a Unit up, which needs a Map, which needs the database.
     */
    struct MoveProfile
    {
        /// NAV_* areas this mover may occupy, as Detour's include/exclude masks.
        uint16 includeFlags = 0;
        uint16 excludeFlags = 0;

        bool canSwim = false;
        bool canFly = false;

        /**
         * @brief May this mover travel where the navmesh does not describe the ground?
         *
         * Creatures may; players never do. A client drives its own movement and would be
         * desynchronised by a server route through geometry it can walk into, so the
         * exemption was always for creatures -- the old type test merely said so
         * indirectly, by casting.
         */
        bool mayLeaveMesh = false;

        /// The mover is exempt from routing entirely (UNIT_STAT_IGNORE_PATHFINDING).
        bool ignorePathfinding = false;

        /**
         * @brief May this mover cross off-mesh ground?
         *
         * The geometry has already answered "not on the mesh"; this decides whether that is
         * fatal, or merely means the mover swims or flies over it.
         *
         * @param underWater The off-mesh ground in question is under water.
         */
        bool MayGoDirect(bool underWater) const
        {
            return mayLeaveMesh && (underWater ? canSwim : canFly);
        }
    };

    /**
     * @brief Snapshot what `mover` may do right now.
     *
     * THE one place that reads a Unit in order to decide movement policy. It is a snapshot
     * and not a cache: the water exemption below depends on where the mover is standing at
     * this instant, so it is taken per routing request rather than once per mover.
     */
    MoveProfile ProfileOf(Unit const& mover);
}

#endif // MANGOS_MOVEPROFILE_H
