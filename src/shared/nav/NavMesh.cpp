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

#include "nav/NavMesh.hpp"

#include "nav/NavGrid.hpp"

#include <cmath>

namespace Nav
{
    namespace
    {
        constexpr int SIDE = CELLS_PER_TILE;

        /// The cell just outside a rectangle's edge, at position `along` on that edge.
        /// Returns false when the edge is the tile's own rim: a portal never leaves the
        /// tile, because what joins two tiles is matched by the store when both are
        /// resident and neither file may refer to the other's indices.
        bool OutsideCell(const NavRect& rect, uint8_t side, int along, int& outX,
                         int& outY, int& inX, int& inY)
        {
            switch (side)
            {
                case SIDE_MINUS_X:
                    inX = rect.x0;
                    inY = along;
                    outX = inX - 1;
                    outY = along;
                    break;
                case SIDE_PLUS_X:
                    inX = rect.x1;
                    inY = along;
                    outX = inX + 1;
                    outY = along;
                    break;
                case SIDE_MINUS_Y:
                    inX = along;
                    inY = rect.y0;
                    outX = along;
                    outY = inY - 1;
                    break;
                default:
                    inX = along;
                    inY = rect.y1;
                    outX = along;
                    outY = inY + 1;
                    break;
            }

            return outX >= 0 && outX < SIDE && outY >= 0 && outY < SIDE;
        }

        void EdgeRange(const NavRect& rect, uint8_t side, int& lo, int& hi)
        {
            const bool alongY = side == SIDE_MINUS_X || side == SIDE_PLUS_X;
            lo = alongY ? rect.y0 : rect.x0;
            hi = alongY ? rect.y1 : rect.x1;
        }
    }

    TileMesh BuildTileMesh(const NavTile& tile)
    {
        const TilePlan plan = ReadTilePlan(tile);

        TileMesh mesh;
        std::vector<int32_t> cellToRect;
        mesh.rects = DecomposeTile(tile, plan, &cellToRect);
        mesh.first.assign(mesh.rects.size() + 1, 0);

        // The one step rule, shared with the flood that built the regions, with the tile
        // stitcher and with the router. A mesh that opened where they did not would let a
        // search cross ground they call separate.
        const float window = ClimbWindow(tile.Params().maxClimb,
                                         tile.Params().maxSlopeDeg, CELL_SIZE);

        for (size_t r = 0; r < mesh.rects.size(); ++r)
        {
            const NavRect& rect = mesh.rects[r];
            mesh.first[r] = uint32_t(mesh.portals.size());

            for (uint8_t side = 0; side < 4; ++side)
            {
                int lo = 0;
                int hi = 0;
                EdgeRange(rect, side, lo, hi);

                // Runs are accumulated rather than emitted per cell: a doorway is one
                // opening however many cells wide it is, and the interval is what the
                // search interpolates over.
                int32_t runNeighbour = -1;
                int runFrom = 0;

                const auto flush = [&](int runTo)
                {
                    if (runNeighbour < 0)
                    {
                        return;
                    }

                    Portal portal;
                    portal.rect = uint32_t(r);
                    portal.neighbour = uint32_t(runNeighbour);
                    portal.side = side;
                    portal.lo = uint16_t(runFrom);
                    portal.hi = uint16_t(runTo);
                    mesh.portals.push_back(portal);
                    runNeighbour = -1;
                };

                for (int along = lo; along <= hi; ++along)
                {
                    int outX = 0, outY = 0, inX = 0, inY = 0;
                    int32_t neighbour = -1;

                    if (OutsideCell(rect, side, along, outX, outY, inX, inY))
                    {
                        const size_t inside = size_t(inX) * SIDE + size_t(inY);
                        const size_t outside = size_t(outX) * SIDE + size_t(outY);

                        const bool joined =
                            plan.Walkable(int(inside)) && plan.Walkable(int(outside)) &&
                            plan.region[inside] == plan.region[outside] &&
                            std::fabs(plan.z[inside] - plan.z[outside]) <= window;

                        if (joined)
                        {
                            neighbour = cellToRect[outside];
                        }
                    }

                    if (neighbour != runNeighbour)
                    {
                        flush(along - 1);
                        if (neighbour >= 0)
                        {
                            runNeighbour = neighbour;
                            runFrom = along;
                        }
                    }
                }

                flush(hi);
            }
        }

        mesh.first[mesh.rects.size()] = uint32_t(mesh.portals.size());
        return mesh;
    }

    void PortalSegment(const NavTile& tile, const TileMesh& mesh, const Portal& portal,
                       float& ax, float& ay, float& bx, float& by)
    {
        const NavRect& rect = mesh.rects[portal.rect];
        const bool alongY = portal.side == SIDE_MINUS_X || portal.side == SIDE_PLUS_X;

        // Cell indices grow as world coordinates fall, so the low index is the HIGH
        // world coordinate and the outer rim of it is half a cell further out still.
        const int globalLo =
            alongY ? GlobalCell(tile.TileY(), portal.lo) : GlobalCell(tile.TileX(), portal.lo);
        const int globalHi =
            alongY ? GlobalCell(tile.TileY(), portal.hi) : GlobalCell(tile.TileX(), portal.hi);

        const float from = CellCentre(globalLo) + CELL_SIZE * 0.5f;
        const float to = CellCentre(globalHi) - CELL_SIZE * 0.5f;

        // The fixed axis is the boundary the run lies on: between the rectangle's border
        // row and the row outside it, so half a cell beyond that row's centre.
        float fixed = 0.0f;
        switch (portal.side)
        {
            case SIDE_MINUS_X:
                fixed = CellCentre(GlobalCell(tile.TileX(), rect.x0)) + CELL_SIZE * 0.5f;
                break;
            case SIDE_PLUS_X:
                fixed = CellCentre(GlobalCell(tile.TileX(), rect.x1)) - CELL_SIZE * 0.5f;
                break;
            case SIDE_MINUS_Y:
                fixed = CellCentre(GlobalCell(tile.TileY(), rect.y0)) + CELL_SIZE * 0.5f;
                break;
            default:
                fixed = CellCentre(GlobalCell(tile.TileY(), rect.y1)) - CELL_SIZE * 0.5f;
                break;
        }

        if (alongY)
        {
            ax = fixed;
            bx = fixed;
            ay = from;
            by = to;
        }
        else
        {
            ay = fixed;
            by = fixed;
            ax = from;
            bx = to;
        }
    }
}
