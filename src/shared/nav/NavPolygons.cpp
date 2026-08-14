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

#include "nav/NavPolygons.hpp"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"

#include <algorithm>
#include <limits>

namespace Nav
{
    namespace
    {
        constexpr uint16_t NO_REGION = 0xFFFF;
        constexpr int SIDE = CELLS_PER_TILE;

        /// The flattened plan of the tile: which region owns each cell, and which of
        /// that cell's surfaces it is. Resolved once, because `SurfacesAt` allocates
        /// into a vector and the scan below reads every cell several times.
        struct Plan
        {
            std::vector<uint16_t> region;
            std::vector<uint16_t> layer;

            uint16_t RegionAt(int x, int y) const
            {
                return region[size_t(x) * SIDE + size_t(y)];
            }
        };

        Plan ReadPlan(const NavTile& tile)
        {
            Plan plan;
            plan.region.assign(size_t(SIDE) * SIDE, NO_REGION);
            plan.layer.assign(size_t(SIDE) * SIDE, 0);

            std::vector<Surface> surfaces;

            for (int x = 0; x < SIDE; ++x)
            {
                for (int y = 0; y < SIDE; ++y)
                {
                    const int cell = x * SIDE + y;
                    tile.SurfacesAt(cell, surfaces);
                    if (surfaces.empty())
                    {
                        continue;
                    }

                    // The lowest walkable surface owns the plan. Two surfaces of one cell
                    // cannot both be in one rectangle -- a rectangle is an area, not a
                    // volume -- and the lowest is the ground a mover crossing the square
                    // in plan is on. Stacked floors keep their own regions and their own
                    // rectangles; what is lost here is only the ability to say which of
                    // two stacked surfaces a rectangle meant, and the region says that.
                    const Surface* pick = nullptr;
                    for (const Surface& s : surfaces)
                    {
                        if (!s.Valid())
                        {
                            continue;
                        }
                        if (!pick || s.z < pick->z)
                        {
                            pick = &s;
                        }
                    }

                    if (pick)
                    {
                        plan.region[size_t(cell)] = pick->region;
                        plan.layer[size_t(cell)] = pick->layer;
                    }
                }
            }

            return plan;
        }
    }

    uint32_t WalkableCellCount(const NavTile& tile)
    {
        uint32_t count = 0;
        std::vector<Surface> surfaces;

        for (int cell = 0; cell < SIDE * SIDE; ++cell)
        {
            tile.SurfacesAt(cell, surfaces);
            for (const Surface& s : surfaces)
            {
                if (s.Valid())
                {
                    ++count;
                    break;
                }
            }
        }

        return count;
    }

    std::vector<NavRect> DecomposeTile(const NavTile& tile)
    {
        const Plan plan = ReadPlan(tile);

        std::vector<uint8_t> taken(size_t(SIDE) * SIDE, 0);
        std::vector<NavRect> out;

        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                const size_t seed = size_t(x) * SIDE + size_t(y);
                const uint16_t region = plan.region[seed];
                if (region == NO_REGION || taken[seed])
                {
                    continue;
                }

                // Along y first, because y is the contiguous index and a run of open
                // ground is a run in memory.
                int y1 = y;
                while (y1 + 1 < SIDE)
                {
                    const size_t next = size_t(x) * SIDE + size_t(y1 + 1);
                    if (plan.region[next] != region || taken[next])
                    {
                        break;
                    }
                    ++y1;
                }

                // Then down in x, one whole row at a time: a row that is not entirely
                // ours stops the growth rather than being partly swallowed, which is
                // what keeps the result a partition instead of an overlapping cover.
                int x1 = x;
                while (x1 + 1 < SIDE)
                {
                    bool wholeRow = true;
                    for (int scan = y; scan <= y1 && wholeRow; ++scan)
                    {
                        const size_t next = size_t(x1 + 1) * SIDE + size_t(scan);
                        wholeRow = plan.region[next] == region && !taken[next];
                    }

                    if (!wholeRow)
                    {
                        break;
                    }
                    ++x1;
                }

                NavRect rect;
                rect.x0 = uint16_t(x);
                rect.y0 = uint16_t(y);
                rect.x1 = uint16_t(x1);
                rect.y1 = uint16_t(y1);
                rect.region = region;
                rect.minZ = std::numeric_limits<float>::max();
                rect.maxZ = -std::numeric_limits<float>::max();
                rect.clearance = 0xFF;

                for (int ax = x; ax <= x1; ++ax)
                {
                    for (int ay = y; ay <= y1; ++ay)
                    {
                        const size_t at = size_t(ax) * SIDE + size_t(ay);
                        taken[at] = 1;

                        const Surface s =
                            tile.SurfaceAt(int(at), plan.layer[at]);
                        if (!s.Valid())
                        {
                            continue;
                        }

                        rect.minZ = std::min(rect.minZ, s.z);
                        rect.maxZ = std::max(rect.maxZ, s.z);
                        rect.clearance = std::min(rect.clearance, s.clearance);
                    }
                }

                out.push_back(rect);
            }
        }

        return out;
    }
}
