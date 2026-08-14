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

#pragma once

/**
 * @file Polyanya.hpp
 * @brief THE SHORTEST PATH OVER A CONVEX-POLYGON MESH, NOT AN APPROXIMATION OF ONE.
 *
 * After Cui, Harabor & Grastien, "Compromise-free Pathfinding on a Navigation Mesh"
 * (IJCAI 2017), which generalises Anya from grids to meshes.
 *
 * == Why this and not what Detour does ==
 *
 * Detour runs A* over polygon centres and then straightens the result with the funnel
 * algorithm. That is two decisions in sequence and only the second one is geometric: the
 * graph search has already committed to a sequence of polygons, and the funnel can only
 * pull the path taut WITHIN that corridor. If a shorter route exists through a different
 * part of the same doorway -- or through a different doorway of the same polygon -- the
 * funnel cannot recover it, because the corridor it was handed does not contain it.
 *
 * The search here never commits to a corridor. Its states are INTERVALS of points on the
 * openings, carried together, so a whole continuum of routes stays alive until the
 * geometry itself separates them. The result is the true Euclidean shortest path over the
 * mesh, in one pass, with no smoothing step afterwards.
 *
 * == What a search node is ==
 *
 * A root point, and an interval on one opening seen from that root. `g` is the distance
 * from the start to the root; the rest of the cost is straight-line, because inside a
 * convex polygon a straight line between two points on its boundary stays inside it.
 * That is the property the whole method rests on and the reason the areas must be convex.
 *
 * The root only moves when the path must BEND, which happens at a corner of the mesh --
 * so the roots along a finished path are exactly its turning points, and the path comes
 * out of the search already taut. There is nothing to straighten.
 */

#include "Geometry/Vector3.h"
#include "nav/NavMesh.hpp"
#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    /**
     * @brief What a query is asked, in the tile's own frame.
     *
     * One tile at a time on purpose. Crossing tiles is the store's business -- it is
     * what matches a border when both sides are resident -- and folding it in here would
     * put the same seam logic in two places. The router walks tile to tile; this answers
     * "the shortest way across THIS one".
     */
    struct MeshQuery
    {
        float startX = 0.0f;
        float startY = 0.0f;
        float startZ = 0.0f;
        float endX = 0.0f;
        float endY = 0.0f;

        /// The heights matter, and only where floors stack. A bridge and the ground
        /// under it are the same square in plan, so a query given two positions without
        /// them cannot say which floor it means -- and picking the lower one silently is
        /// how two points sixty yards apart in Blackrock Depths came back unroutable.
        float endZ = 0.0f;

        /// A mover narrower than a rectangle's recorded clearance may cross it. Zero
        /// admits every area, which is what a query that does not care about width wants.
        float radius = 0.0f;

        /// Refuse to expand more than this many nodes. A budget, not a correctness
        /// device: the answer is optimal or there is no answer.
        uint32_t maxExpansions = 20000;
    };

    struct MeshPath
    {
        bool found = false;

        /// The turning points, start and end included. Already taut: these are the roots
        /// the search bent around, in order.
        std::vector<Geometry::Vector3> points;

        /// Length in yards, measured in plan. Exact for the mesh it was found on.
        float length = 0.0f;

        /// How many search nodes were expanded. Reported so a regression in the
        /// heuristic shows up as work rather than as a wrong answer.
        uint32_t expansions = 0;
    };

    /**
     * @brief The shortest path across one tile's mesh, or nothing.
     *
     * @param tile  the tile the mesh was derived from, for its frame and its heights
     * @param mesh  from `BuildTileMesh`
     */
    MeshPath FindMeshPath(const NavTile& tile, const TileMesh& mesh,
                          const MeshQuery& query);
}
