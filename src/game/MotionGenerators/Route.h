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

#ifndef MANGOS_ROUTE_H
#define MANGOS_ROUTE_H

// The answer to a routing request, and NOTHING that produces one. This header pulls in
// a vector of points and no more: no navmesh, no Detour, no Unit, no Map. That is what
// lets the states below be asserted on in a test that links none of the server -- and
// the reason the states were worth extracting in the first place is that, as a bitmask
// inside the router, they could only ever be read by something that had a world.

#include "Geometry/Vector3.h"
#include "Platform/Define.h"

#include <vector>

namespace Nav
{
    /**
     * @brief What a routing attempt produced.
     *
     * An enumeration and not a bitmask, deliberately. The mask this replaces let states be
     * combined that nobody designed -- a result was routinely both "normal" and "not using
     * a path" -- and every consumer had to know which half of the field answered its own
     * question. Four states, mutually exclusive, and every consumer reads one field.
     */
    enum class RouteOutcome : uint8
    {
        Routed,      ///< Real geometry, all the way to the goal.
        Partial,     ///< Real geometry, stopped short of the goal.
        Direct,      ///< No routing was used and that is ACCEPTABLE -- a swimmer or a flier
                     ///< off the mesh, or a map with no navmesh at all. The points are a
                     ///< straight line laid onto the ground.
        Unroutable   ///< No route, and no fallback the mover is entitled to.
    };

    /**
     * @brief Why the route ended where it did.
     *
     * Distinct from the outcome because "stopped short" has causes that call for opposite
     * responses: a wall means the goal is unreachable from here, while a budget means the
     * search gave up and re-planning from further along makes progress.
     */
    enum class RouteStop : uint8
    {
        Reached,      ///< Arrived at the goal.
        Wall,         ///< The world stopped it short.
        NodeBudget,   ///< The search exhausted its node pool.
        PolyBudget,   ///< The corridor filled the polygon buffer.
        NoMesh,       ///< No navmesh here, or the mover is exempt from using one.
        OffMesh,      ///< The start or the goal does not sit on the mesh.
        Forced,       ///< The caller demanded this destination whatever the geometry says.
        Failed        ///< The query itself errored.
    };

    /**
     * @brief The answer to one routing request: a value, owned by whoever asked.
     *
     * The point type is spelled out rather than taken from Movement::PointsArray so that
     * this header owes nothing to the spline code. They are the same type -- PointsArray is
     * a vector of Geometry::Vector3 under a using-declaration -- so the two interoperate
     * without a conversion.
     */
    struct Route
    {
        std::vector<Geometry::Vector3> points;
        RouteOutcome outcome = RouteOutcome::Unroutable;
        RouteStop    stop = RouteStop::Failed;

        /// Reached the goal on real geometry. The strictest of the three.
        bool IsRouted() const { return outcome == RouteOutcome::Routed; }

        /**
         * @brief The points came off the navmesh rather than out of a straight line.
         *
         * True of a PARTIAL route as well: stopping short does not make the geometry that
         * was walked any less real, and welding one leg to the next is safe on it. This is
         * the question a map with no navmesh has to answer NO to, which a "did it fail"
         * test cannot -- such a map fails nothing, it just answers every query with a line.
         */
        bool UsedGeometry() const
        {
            return outcome == RouteOutcome::Routed || outcome == RouteOutcome::Partial;
        }

        /// The mover will arrive: routed the whole way, or deliberately going direct.
        bool WillArrive() const
        {
            return outcome == RouteOutcome::Routed || outcome == RouteOutcome::Direct;
        }

        /// Nothing usable came back.
        bool Failed() const { return outcome == RouteOutcome::Unroutable; }
    };
}

#endif // MANGOS_ROUTE_H
