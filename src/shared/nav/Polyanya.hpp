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
 *
 * == What a MoveProfile can and cannot ask of it ==
 *
 * PERMISSION is exact here. An area the profile does not admit -- lava for anything
 * alive, the surface of a bay for a creature that walks the bottom of it -- is not part
 * of the mesh this search sees, so the path it returns is the shortest one over the
 * ground that mover is actually allowed to stand on.
 *
 * PREFERENCE is not, and cannot be, without this ceasing to be Polyanya. `areaCost`
 * makes the problem a weighted-region shortest path, where the optimal route REFRACTS at
 * the boundary between two costs by Snell's law instead of running straight -- and the
 * straight line inside a convex area is the single property every interval projection
 * here rests on. Pricing areas inside this loop would not make it slower or approximate;
 * it would make its answer wrong while it went on claiming to be optimal.
 *
 * So cost is applied where the search really is over discrete areas: the coarse stage in
 * `Router`, which is what decides WHICH areas the corridor passes through. Within one
 * area-to-area hop the route is straight and the cost is a constant, so the two agree.
 * What is left is a shorter path through admitted-but-dearer ground being preferred to a
 * longer one beside it, inside a single tile. That is a real limitation, it is written
 * down in `NAV.md`, and it is the honest price of an exact geometric answer.
 */

#include "Geometry/Vector3.h"
#include "nav/NavArea.hpp"
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

        /**
         * @brief WHAT THE MOVER IS, not merely how wide.
         *
         * This was a bare radius, and a bare radius is half a permission. A rectangle
         * carries the area it is made of exactly so a swimmer and a walker can be told
         * apart, and a search that read only the width sent walkers through lava and
         * across the surface of bays -- while the cell engine, asked the same question
         * about the same ground, refused. One question with two answers, and this is the
         * field that had gone missing from one of them.
         *
         * A default profile admits ground and shallow water at zero radius, which is
         * what a query that does not care wants.
         */
        MoveProfile profile;

        /// Refuse to expand more than this many nodes. A budget, not a correctness
        /// device: the answer is optimal or there is no answer.
        uint32_t maxExpansions = 20000;

        /**
         * @brief A ROUTE THE CALLER ALREADY HAS, so areas that cannot beat it are skipped.
         *
         * After Koch & Funke (SoCS 2025): with any real route in hand, every area whose
         * cheapest possible detour already exceeds it is provably not on the shortest
         * path and need not be expanded. See `MeshBound.hpp` for the bound and why it
         * keeps the answer optimal.
         *
         * The straight-line heuristic this supplements is at its worst exactly where a
         * bay or an inlet doubles back -- it pours the search into the water because the
         * target is on the far shore -- and that is the geometry these tiles are full of.
         *
         * MUST BE ACHIEVABLE. A number smaller than the true optimum discards the
         * optimum. Zero means "none known", which is the honest default and disables the
         * test entirely.
         */
        float upperBound = 0.0f;
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

        /**
         * @brief The cap stopped the search; it did not finish on its own.
         *
         * A path found this way is whatever was in hand when the budget ran out, not
         * the shortest one -- and `found` alone said nothing about the difference. A
         * capped search once reported a 119-yard hairpin for a 30-yard walk and the
         * router passed it on as "reached the goal".
         */
        bool exhausted = false;
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
