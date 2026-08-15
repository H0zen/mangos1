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

        /**
         * @brief The packed area and flags every cell of it shares.
         *
         * Not decoration, and not derivable from anything else here: a `MoveProfile`
         * admits a mover to ground, to shallow water, to deep water, to lava or to none
         * of them, and without this the polygon layer cannot tell a swimmer from a
         * walker. A rectangle only ever covers cells of ONE area, because growth stops
         * where the area changes -- the shoreline is a rectangle boundary, which is what
         * it should be anyway.
         */
        uint8_t area = 0;

        /**
         * @brief Which surface of its cells this rectangle is.
         *
         * A bridge over ground is two walkable surfaces in the same square of plan, and
         * one rectangle can only be one of them. Rectangles are built per layer and
         * carry which -- so a route over the bridge and a route under it are different
         * areas of the mesh rather than the same one seen twice.
         */
        uint16_t layer = 0;

        /// Height range over the covered cells. A rectangle is flat in plan, never in Z:
        /// it is a piece of ground, and the search seats its points on that ground.
        float minZ = 0.0f;
        float maxZ = 0.0f;

        /**
         * @brief NO HEIGHT LIVES HERE, and that was measured rather than assumed.
         *
         * The first attempt fitted a plane per rectangle and refused any cell that
         * pushed the residual past a fifth of a yard. It works, and it is the wrong
         * shape: over map 0 it took the partition from 1,480,966 rectangles to
         * 22,949,010 -- from 112 cells each to 7.2.
         *
         * The reason is structural and not a badly chosen threshold. The ADT heightmap
         * is piecewise linear with a break every four NAV cells (128 height cells to 512
         * nav cells), so a plane cannot span a triangle edge; forcing the areas to be
         * planar fragments them at exactly the terrain's own resolution, and 7.2 cells
         * is under two height cells.
         *
         * Height is a smooth field and belongs stored as one. `TileMesh::heights` keeps
         * it at the terrain's own 129 x 129, quantised -- about 33 KB a tile against the
         * 2.5 MB of per-cell region, area, clearance and layer that the rectangles DO
         * replace. Areas carry what is constant over an area; the field carries what
         * varies smoothly. Mixing the two costs fifteen times the geometry.
         */

        /**
         * @brief The WIDEST clearance over the covered cells, not the narrowest.
         *
         * The narrowest is the intuitive choice and it is unusable, which took a
         * measurement to see. `Clearance()` marks every node with a missing neighbour as
         * a border and gives it half a cell -- 0.52 yards -- so a maximal rectangle,
         * which by construction runs right up to the edge of the walkable set, always
         * contains such a cell. Open ground came back with a clearance of half a yard,
         * and every mover wider than that was refused Elwynn on the grounds that it is
         * too narrow.
         *
         * So this is the room the rectangle offers at its most generous, which is what a
         * rectangle-level test can honestly claim: it says a mover MIGHT fit, and the
         * per-point clearance decides whether it does. Using it as the width filter
         * outright is what cancelled the whole reason clearance is stored per cell.
         */
        uint8_t clearance = 0;

        uint32_t Cells() const
        {
            return uint32_t(x1 - x0 + 1) * uint32_t(y1 - y0 + 1);
        }
    };

    /**
     * @brief A tile's walkable set flattened to one surface per cell.
     *
     * Resolved once and passed around, because `SurfacesAt` fills a vector and every
     * pass over a tile reads each cell several times. The lowest walkable surface owns
     * the plan: a rectangle is an area and not a volume, so it can hold at most one
     * surface per cell, and the lowest is the ground a mover crossing the square in plan
     * stands on. Stacked floors keep their own regions and their own rectangles.
     */
    struct TilePlan
    {
        static constexpr uint16_t NO_REGION = 0xFFFF;

        std::vector<uint16_t> region;   ///< NO_REGION where nothing is walkable
        std::vector<uint16_t> layer;    ///< which of the cell's surfaces the plan means
        std::vector<float> z;           ///< that surface's height
        std::vector<uint8_t> area;      ///< packed area and flags

        /// Which surface of each cell this plan took: 0 is the lowest, 1 the next up.
        /// Carried so the rectangles built from it can say which floor they are.
        uint16_t layerIndex = 0;

        bool Walkable(int cell) const
        {
            return region[static_cast<size_t>(cell)] != NO_REGION;
        }
    };

    /// The lowest walkable surface of every cell.
    TilePlan ReadTilePlan(const NavTile& tile);

    /**
     * @brief One plan per floor, lowest first.
     *
     * A bridge over ground is two walkable surfaces in one square of plan, and a
     * rectangle can only be one of them. Decomposing each plan separately is what makes
     * the two different areas of the mesh instead of the same one seen twice -- so a
     * route over the bridge and a route under it stop being the same route.
     *
     * The first plan is always present when anything is walkable; the second and beyond
     * exist only where floors actually stack, which in 2.4.3 is buildings, bridges and
     * ship decks rather than open country.
     */
    std::vector<TilePlan> ReadTilePlans(const NavTile& tile);

    /**
     * @brief Height samples along one edge of a tile's height field.
     *
     * The terrain's own V9 grid, mirrored: 129 x 129 corners over 128 height cells,
     * which is exactly four nav cells each. Not a resampling and not a choice -- reading
     * the field at the resolution it was authored at is what makes the samples exact at
     * every corner, and anything finer would store interpolation as if it were data.
     */
    constexpr int HEIGHT_SIDE = (CELLS_PER_TILE / 4) + 1;

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

    /**
     * @brief The same partition, over a plan already read, and saying where each cell went.
     *
     * `cellToRect` is filled with one index per cell -- the rectangle covering it, or -1
     * where nothing is walkable. That map is what turns a partition into a MESH: two
     * rectangles are neighbours along the cells where one's border cell faces the
     * other's, and finding that without the map means searching the rectangle list per
     * cell.
     */
    std::vector<NavRect> DecomposeTile(const NavTile& tile, const TilePlan& plan,
                                       std::vector<int32_t>* cellToRect);

    /// How many of the tile's cells carry a walkable surface. The number the rectangle
    /// count is worth comparing against, and the invariant a test checks against a sum.
    uint32_t WalkableCellCount(const NavTile& tile);
}
