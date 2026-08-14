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
 * @file NavPolygons.hpp
 * @brief THE WALKABLE SET AS AREAS INSTEAD OF CELLS.
 *
 * A tile stores 512 x 512 cells and a stack of surfaces over each. That is what makes
 * one map cost 1.7 GB, and the cost is paid twice: on disk, and again in every search
 * that visits cells one at a time when the ground between them never changed.
 *
 * The literature settled this. A navigation mesh is a set of CONVEX regions; a search
 * moves between regions and interpolates inside them, and the good query algorithms are
 * defined over exactly that. Polyanya (Cui, Harabor & Grastien, IJCAI 2017) returns the
 * optimal any-angle path in a single search over convex polygons -- strictly better than
 * Detour's A*-then-funnel, which is not optimal -- and it needs the polygons to exist
 * before it can be written. This is that step, and nothing more.
 *
 * == Why rectangles, and why that is not a compromise ==
 *
 * The cover produced here is a PARTITION of the walkable cells into axis-aligned
 * rectangles, greedily maximal. Three reasons, in order of weight:
 *
 * - It is exactly faithful. Every walkable cell is in one rectangle and no rectangle
 *   contains a cell that was not walkable, so the polygon layer describes the same set
 *   of standing places the cell grid did. A contour-and-triangulate pass has to decide
 *   what to do with a staircase boundary and can only be checked by eye.
 * - Rectangles are convex, which is the only property Polyanya asks of them.
 * - The boundary it produces is the boundary the cells already had. A route over these
 *   polygons hugs the same staircase the fine cell search hugs today, so switching the
 *   query cannot make a path worse than the one being replaced -- and that is what makes
 *   the switch measurable rather than a matter of taste.
 *
 * The medial-axis structure (van Toll, Cook, van Kreveld & Geraerts) is the destination
 * for the clearance side of this -- it is O(boundary) rather than O(cells) and answers
 * any radius exactly instead of a byte per cell. It is a later step and it replaces the
 * grid outright; this one leaves the grid where it is and adds a description of it.
 */

#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    /**
     * @brief One convex piece of a region: a rectangle of cells, inclusive at both ends.
     *
     * In-tile cell coordinates, the same ones `NavTile` indexes with -- `x` is the first
     * index and `y` the second, so the cell index of a corner is `x * CELLS_PER_TILE + y`.
     * Kept in cells rather than yards because the grid is what produced them and a
     * conversion here would be a second place for the axis convention to be got wrong.
     */
    struct NavRect
    {
        uint16_t x0 = 0;
        uint16_t y0 = 0;
        uint16_t x1 = 0;   ///< inclusive
        uint16_t y1 = 0;   ///< inclusive

        uint16_t region = 0;

        /// Height range over the covered cells. A rectangle is flat in plan, never in Z:
        /// it is a piece of ground, and the search seats its points on that ground.
        float minZ = 0.0f;
        float maxZ = 0.0f;

        /// The NARROWEST clearance over the covered cells, in the packed byte the cells
        /// carry. Conservative on purpose: a mover that fits this fits everywhere in the
        /// rectangle, which is what makes a per-rectangle test sound.
        uint8_t clearance = 0;

        uint32_t Cells() const
        {
            return uint32_t(x1 - x0 + 1) * uint32_t(y1 - y0 + 1);
        }
    };

    /**
     * @brief Partition a tile's walkable cells into maximal axis-aligned rectangles.
     *
     * Grouped by REGION and not by (region, layer): a region already means "one connected
     * walkable surface", while a layer index is only a position in a cell's own stack --
     * a bridge is layer 1 where there is ground beneath it and layer 0 where there is
     * not, so grouping by layer would cut the bridge in half at the point it leaves the
     * shore. A rectangle is an area in plan, so it can hold at most one surface per cell
     * anyway, and the region says which.
     *
     * @return The rectangles, in scan order. Empty when the tile has no walkable cell.
     */
    std::vector<NavRect> DecomposeTile(const NavTile& tile);

    /// How many of the tile's cells carry a walkable surface. The number the rectangle
    /// count is worth comparing against, and the invariant a test checks against a sum.
    uint32_t WalkableCellCount(const NavTile& tile);
}
