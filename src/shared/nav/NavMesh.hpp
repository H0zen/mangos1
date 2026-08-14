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
 * @file NavMesh.hpp
 * @brief CONVEX AREAS PLUS THE OPENINGS BETWEEN THEM -- a navigation mesh, properly.
 *
 * `NavPolygons` turns a tile's cells into convex rectangles. Rectangles alone are a
 * picture, not a mesh: what makes a mesh searchable is knowing, for each one, exactly
 * WHERE a mover may leave it and into which neighbour. That opening is a segment, not a
 * link, and the distinction is the whole reason the good query algorithms are faster and
 * shorter than a graph search.
 *
 * Polyanya (Cui, Harabor & Grastien, IJCAI 2017) searches over intervals of points taken
 * from these segments rather than over discrete nodes, and returns the optimal any-angle
 * path in a single pass. Detour's A*-over-polygons-then-funnel needs the same structure
 * and settles for less: the funnel straightens a path the graph search already committed
 * to, so it cannot recover a shorter route through a different opening.
 *
 * == What a portal is here ==
 *
 * A maximal run of cell boundary along one side of a rectangle where the cell on the
 * other side belongs to one particular neighbour AND the step between them is a step.
 * Both halves matter. Two rectangles can share a hundred cells of border and be joined
 * along only twenty of them -- a ledge that runs beside a drop for most of its length --
 * and a mesh that recorded the whole shared edge as passable would send routes off it.
 *
 * The step test is `Nav::ClimbWindow`, the same function the cell flood, the tile
 * stitcher and the router already use. It has to be: a mesh that opened where the flood
 * did not would let a search cross ground the regions say is separate.
 */

#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    /**
     * @brief One opening between two rectangles, as an interval of cell boundary.
     *
     * `side` says which edge of `rect` this is, and therefore which axis `lo`..`hi`
     * runs along:
     *
     *   SIDE_MINUS_X  the edge at x = rect.x0, interval in y
     *   SIDE_PLUS_X   the edge at x = rect.x1 + 1, interval in y
     *   SIDE_MINUS_Y  the edge at y = rect.y0, interval in x
     *   SIDE_PLUS_Y   the edge at y = rect.y1 + 1, interval in x
     *
     * The interval is in CELL indices and inclusive at both ends: cells `lo` through
     * `hi` of this rectangle's border row face the neighbour across a step a mover can
     * take. Turning that into a world segment is `PortalSegment`, which is where the
     * half-cell offsets live so that nothing else has to know them.
     */
    struct Portal
    {
        uint32_t rect = 0;
        uint32_t neighbour = 0;
        uint8_t side = 0;
        uint16_t lo = 0;
        uint16_t hi = 0;    ///< inclusive

        uint16_t Cells() const { return uint16_t(hi - lo + 1); }
    };

    enum PortalSide : uint8_t
    {
        SIDE_MINUS_X = 0,
        SIDE_PLUS_X = 1,
        SIDE_MINUS_Y = 2,
        SIDE_PLUS_Y = 3,
    };

    /**
     * @brief A tile's walkable set as convex areas and the openings between them.
     *
     * Built, not stored: the cell grid is still what the file carries, and this is
     * derived from it in one pass. When the mesh becomes the stored form, this struct is
     * what gets written and the derivation disappears -- which is why nothing in it
     * refers back to the cells it came from.
     */
    struct TileMesh
    {
        std::vector<NavRect> rects;

        /// All portals, grouped by rectangle. Rectangle `r` owns
        /// `portals[first[r] .. first[r + 1])`.
        std::vector<Portal> portals;
        std::vector<uint32_t> first;

        size_t PortalCount(uint32_t rect) const
        {
            return first[rect + 1] - first[rect];
        }
    };

    /**
     * @brief Derive the mesh of one tile.
     *
     * @param tile   the baked tile, read for its surfaces and its bake parameters
     * @return the rectangles and every opening between them. Portals are symmetric:
     *         a run from A to B has an identical run from B to A.
     */
    TileMesh BuildTileMesh(const NavTile& tile);

    /**
     * @brief The world-space segment a portal spans, on the tile it belongs to.
     *
     * The interval is a run of cells, and the opening is the boundary BETWEEN those
     * cells and their neighbours: it starts at the outer rim of cell `lo` and ends at
     * the outer rim of cell `hi`, which is half a cell beyond each centre. Getting that
     * wrong shortens every doorway in the world by one cell and is invisible until a
     * route refuses a gap it fits through.
     *
     * @param[out] ax,ay,bx,by the two ends, in world yards.
     */
    void PortalSegment(const NavTile& tile, const TileMesh& mesh, const Portal& portal,
                       float& ax, float& ay, float& bx, float& by);
}
