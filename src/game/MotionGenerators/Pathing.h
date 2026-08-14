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

#ifndef MANGOS_PATHING_H
#define MANGOS_PATHING_H

// The game's side of routing, and the only file in src/game that knows a router exists.
//
// What lives here is everything the search is deliberately not allowed to know:
//
//  - WHO is moving. The router takes a Nav::MoveProfile value; turning a Unit into one
//    is MoveProfile.cpp's job and this is where it is called.
//  - WHETHER routing applies at all -- the config switch, the per-map exclusions, the
//    creature flags that force it on or off.
//  - WHAT TO DO when the ground is not described. The router reports "off mesh" as a
//    fact; only here is it known whether that means a swimmer crossing open water, a
//    flier crossing a canyon, or a walker that simply cannot get there. Laying the
//    straight line needs the terrain's heights, and a router that reached for the
//    terrain could not be tested without one.

#include "MovementIntent.h"
#include "nav/Route.hpp"
#include "nav/SearchBudget.hpp"

#include "MoveProfile.h"

class Unit;

namespace Nav
{
    /**
     * @brief Who may be routed, and where. Server policy, not geometry.
     */
    namespace Policy
    {
        /// Maps routing is switched off for, as the config's comma-separated list.
        void PreventOnMaps(const char* ignoreMapIds);

        /// Release the policy tables at shutdown.
        void Clear();

        /// May this mover be routed on this map at all?
        bool EnabledFor(uint32 mapId, const Unit* unit);

        bool ForceEnabled(const Unit* unit);
        bool ForceDisabled(const Unit* unit);
    }
}

/**
 * @brief One mover's route, from one request to the next.
 *
 * Kept alive across legs by the motion driver, which is why nothing about a request
 * survives into the next one: the profile is re-snapshotted every call and the budget
 * arrives with the request. The router this replaced kept both as members, and an
 * uncapped chase issued after a capped flee silently inherited the flee's cap.
 */
class Pathing
{
    public:
        explicit Pathing(Unit const* owner);

        /// Route on a map the mover is not filed under. A vessel's deck is its own map,
        /// and a boarded unit walks that navigation while the world still holds its guid.
        Pathing(Unit const* owner, uint32 mapId);

        /// Route from where the mover is standing.
        bool calculate(float destX, float destY, float destZ, bool forceDest = false,
                       Nav::SearchBudget budget = Nav::SearchBudget());

        /// Route from an explicit start.
        bool calculate(float startX, float startY, float startZ, float destX,
                       float destY, float destZ, bool forceDest = false,
                       Nav::SearchBudget budget = Nav::SearchBudget());

        /// The whole result of the last calculate(): its points, its outcome, and why
        /// it stopped.
        Nav::Route const& getRoute() const { return m_route; }

        Movement::PointsArray const& getPath() const { return m_route.points; }

        Geometry::Vector3 getStartPosition() const { return m_start; }
        Geometry::Vector3 getEndPosition() const { return m_end; }

        /// The closest point to the destination the route actually reaches.
        Geometry::Vector3 getActualEndPosition() const { return m_actualEnd; }

        /// Is there baked navigation under this mover's map at all?
        bool HasNavigation() const;

    private:
        /**
         * @brief Lay a straight line from start to end, on the ground.
         *
         * Subdivided and snapped to the terrain rather than left as two points, so a
         * mover crossing a slope without navigation does not bunny-hop between the two
         * ends of a chord.
         *
         * Writes no outcome, on purpose: the same line of points is a legitimate route
         * for a flier and a refusal for a walker, and only the caller knows which.
         */
        void BuildShortcut();

        Unit const* m_owner;
        uint32 m_mapId;

        /// Decided once, in the constructor: policy does not change mid-leg.
        bool m_routingAllowed;

        Nav::MoveProfile m_profile;
        Nav::Route m_route;

        Geometry::Vector3 m_start;
        Geometry::Vector3 m_end;
        Geometry::Vector3 m_actualEnd;
};

#endif // MANGOS_PATHING_H
