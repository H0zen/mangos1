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
 * @file MedialAxis.hpp
 * @brief CLEARANCE AS A STRUCTURE, NOT AS A BYTE PER CELL.
 *
 * After van Toll, Cook, van Kreveld & Geraerts, "The Explicit Corridor Map" and "The
 * Medial Axis of a Multi-Layered Environment and its Application as a Navigation Mesh".
 *
 * == What the tile does today, and what it costs ==
 *
 * The bake records, per cell, the distance to the nearest place a mover cannot be. That
 * is the right quantity -- it is what lets one bake serve a murloc and a devilsaur,
 * where Recast erodes the walkable surface by one fixed radius and sends both through
 * the same doorway -- but it is stored as a quantised byte on a quarter of a million
 * cells per tile, and that is where the 1.7 GB of a single map goes.
 *
 * The medial axis carries the same information in O(boundary) instead of O(cells). Every
 * point of the axis is the centre of a maximal disk inside the walkable set; annotate it
 * with the radius of that disk and with the obstacle points the disk touches, and the
 * clearance anywhere is recoverable exactly, for any radius, without storing a field.
 *
 * == The two transforms, and why the second one is the interesting one ==
 *
 * `DistanceField` is an exact Euclidean distance transform, by the lower-envelope method
 * of Felzenszwalb & Huttenlocher: two passes of a one-dimensional transform, rows then
 * columns, linear in the number of cells and exact rather than the chamfer approximation
 * a naive implementation reaches for. It answers "how much room is here".
 *
 * `nearest` is the FEATURE transform that falls out of the same passes: for every cell,
 * WHICH obstacle cell is the closest one. That is the annotation in "Explicit" Corridor
 * Map, and it is what makes the axis extractable without a threshold: a cell lies on the
 * medial axis when two of its neighbours are closest to obstacle points that are far
 * apart -- the point is equidistant from two different pieces of boundary, which is the
 * definition, rather than being a local maximum of a blurred field, which is a heuristic.
 *
 * == What this is not, yet ==
 *
 * This computes the axis on the CELL grid, so its vertices sit at cell centres. The
 * published ECM is exact and continuous, computed from the boundary polygon; that is a
 * later step, and the interface here is written so it can be swapped underneath -- a
 * caller asks for clearance at a point and for the axis vertices near it, never for the
 * grid.
 */

#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    /**
     * @brief One vertex of the medial axis, annotated as the ECM prescribes.
     *
     * Positions are world yards. The clearance is a real distance and not a quantised
     * byte: the whole reason to keep the axis is that the answer stops being rounded to
     * the grid the moment the structure replaces the field.
     */
    struct AxisVertex
    {
        float x = 0.0f;
        float y = 0.0f;

        /// Radius of the maximal disk centred here: the room a mover has at this point.
        float clearance = 0.0f;

        /// The obstacle point the disk touches. Two of them make the axis; one is
        /// recorded because it is what a caller needs to know WHICH side is tight.
        float obstacleX = 0.0f;
        float obstacleY = 0.0f;

        /// The in-tile cell it was found in, so a caller can go back to the grid while
        /// the grid is still what the file carries.
        uint32_t cell = 0;

        uint16_t region = 0;
    };

    /**
     * @brief The distance and feature transforms of one tile's walkable set.
     *
     * Obstacles are the cells nothing may stand on, and the tile's own rim: a mover may
     * be at the border, but the axis of a tile is a statement about THIS tile, and
     * treating the rim as open would put a false corridor down every edge of the map.
     */
    struct DistanceField
    {
        /// Distance to the nearest obstacle cell, in yards, per in-tile cell.
        std::vector<float> distance;

        /// The nearest obstacle cell itself, per in-tile cell. -1 where the tile has no
        /// obstacle at all, which happens on a tile of open ground with the rim excluded.
        std::vector<int32_t> nearest;

        float At(int cell) const { return distance[size_t(cell)]; }
    };

    /// Exact Euclidean distance and feature transform. Linear in cells.
    DistanceField BuildDistanceField(const NavTile& tile, const TilePlan& plan);

    /**
     * @brief The medial axis of one tile, as annotated vertices.
     *
     * A cell is kept when it is equidistant from two pieces of boundary that are far
     * apart -- `separation` yards, the standard pruning parameter, which is what stops
     * every ragged notch in a staircase boundary from growing its own spur.
     */
    std::vector<AxisVertex> BuildMedialAxis(const NavTile& tile, const TilePlan& plan,
                                            const DistanceField& field,
                                            float separation = 2.0f);

    /**
     * @brief How much room there is at a world position, from the field.
     *
     * The bilinear read of the distance transform, so the answer is continuous rather
     * than stepping cell by cell. Negative when the position is not on the tile.
     */
    float ClearanceAt(const NavTile& tile, const DistanceField& field, float x, float y);
}
