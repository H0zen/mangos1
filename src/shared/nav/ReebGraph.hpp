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
 * @file ReebGraph.hpp
 * @brief WHERE THE GROUND CHANGES SHAPE -- the passes, found rather than guessed.
 *
 * The bake already cuts a tile into "regions" and joins them with "gateways", and both
 * are conventions: a region is whatever a flood reached, a gateway is a run of border
 * cells, and a component smaller than six nodes is thrown away because six was a number
 * somebody chose. None of that is wrong, and none of it has a definition.
 *
 * Morse theory gives one. Take a real-valued function on the walkable surface -- height
 * is the natural choice, and the one this uses -- and watch its level sets as the value
 * rises. Almost everywhere nothing happens; at isolated CRITICAL points the topology of
 * the level set changes, and only there:
 *
 *   minimum   a new component is born          the bottom of a hollow
 *   saddle    two components merge             a PASS between two basins
 *   maximum   a component dies                 a summit
 *
 * The Reeb graph is what you get by contracting each connected component of each level
 * set to a point: its nodes are the critical points, its arcs are the components between
 * them. For a walkable surface it is precisely the map of basins and the passes joining
 * them -- which is what a coarse pathfinding stage wants, and what the region-and-gateway
 * structure is an approximation of.
 *
 * == Why this is worth having here ==
 *
 * A saddle is a doorway that nobody had to author and no threshold had to survive. It is
 * the lowest point at which two parts of the ground become one, so it is exactly where a
 * route between them crosses, and its height is exactly the climb that route costs. The
 * current gateways can only express that at a tile border, because that is where the
 * bake happens to cut; a saddle is wherever the ground puts it.
 *
 * == How it is computed ==
 *
 * The standard sweep, which is also the persistent-homology algorithm for zero-dimensional
 * features: sort the cells by height, add them one at a time, and maintain the connected
 * components of what has been added with a union-find. A cell with no lower neighbour
 * starts a component -- a minimum. A cell whose lower neighbours belong to two or more
 * different components merges them -- a saddle. Whichever component is younger dies
 * there, and the height difference between its minimum and the saddle is that feature's
 * PERSISTENCE: how deep the hollow is, in yards, which is a measurement and not a
 * threshold. Features below a persistence anyone cares about can then be cancelled with
 * a reason.
 */

#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    enum class CriticalKind : uint8_t
    {
        Minimum = 0,   ///< a component is born: the bottom of a hollow
        Saddle = 1,    ///< two components merge: a pass
        Maximum = 2,   ///< a component dies: a summit
    };

    /// One point where the shape of the walkable set changes.
    struct CriticalPoint
    {
        CriticalKind kind = CriticalKind::Minimum;

        uint32_t cell = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        /// For a saddle: the two basins it joins, as indices into the minima. For a
        /// minimum or a maximum both are its own basin.
        uint32_t basinA = 0;
        uint32_t basinB = 0;

        /**
         * @brief How much of a feature this is, in yards.
         *
         * The height between a basin's floor and the pass that drowns it. A puddle two
         * inches deep and a canyon both produce a saddle; persistence is what tells them
         * apart, and it is a measurement of the ground rather than a cell count anyone
         * picked. Zero on a minimum that never merges and on a maximum.
         */
        float persistence = 0.0f;
    };

    /**
     * @brief The Reeb graph of one tile's walkable surface under height.
     *
     * `basins` are the minima, in the order they were found; `passes` are the saddles;
     * `summits` the maxima. `arcs` join them: an arc from a basin to the pass that ends
     * it, which is the edge set of the graph.
     */
    struct ReebGraph
    {
        std::vector<CriticalPoint> basins;
        std::vector<CriticalPoint> passes;
        std::vector<CriticalPoint> summits;

        struct Arc
        {
            uint32_t from = 0;   ///< index into `basins`
            uint32_t to = 0;     ///< index into `passes`
            float rise = 0.0f;   ///< height climbed along it, in yards
        };

        std::vector<Arc> arcs;

        size_t CriticalCount() const
        {
            return basins.size() + passes.size() + summits.size();
        }
    };

    /**
     * @brief Sweep one tile's walkable set by height and record where it changes shape.
     *
     * @param minPersistence  Passes that drown a basin shallower than this are not
     *                        recorded, and the basin is merged silently. Not a
     *                        smoothing parameter: it is a statement about what depth of
     *                        hollow is worth calling a place, in yards.
     */
    ReebGraph BuildReebGraph(const NavTile& tile, const TilePlan& plan,
                             float minPersistence = 1.0f);
}
