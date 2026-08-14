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
#include <utility>
#include <vector>

namespace Nav
{
    namespace
    {
        constexpr int SIDE = CELLS_PER_TILE;

    }

    std::vector<TilePlan> ReadTilePlans(const NavTile& tile)
    {
        std::vector<TilePlan> plans;
        std::vector<Surface> surfaces;

        // How deep the deepest stack is decides how many plans there are. Walked first
        // so the plans can be sized once rather than grown a cell at a time.
        size_t deepest = 0;
        for (int cell = 0; cell < SIDE * SIDE; ++cell)
        {
            tile.SurfacesAt(cell, surfaces);
            size_t walkable = 0;
            for (const Surface& s : surfaces)
            {
                walkable += s.Valid() ? 1u : 0u;
            }
            deepest = std::max(deepest, walkable);
        }

        if (deepest == 0)
        {
            return plans;
        }

        plans.resize(deepest);
        for (size_t i = 0; i < deepest; ++i)
        {
            plans[i].region.assign(static_cast<size_t>(SIDE) * SIDE,
                                   TilePlan::NO_REGION);
            plans[i].layer.assign(static_cast<size_t>(SIDE) * SIDE, 0);
            plans[i].z.assign(static_cast<size_t>(SIDE) * SIDE, 0.0f);
            plans[i].area.assign(static_cast<size_t>(SIDE) * SIDE, 0);
            plans[i].layerIndex = static_cast<uint16_t>(i);
        }

        std::vector<const Surface*> stack;

        for (int cell = 0; cell < SIDE * SIDE; ++cell)
        {
            tile.SurfacesAt(cell, surfaces);

            stack.clear();
            for (const Surface& s : surfaces)
            {
                if (s.Valid())
                {
                    stack.push_back(&s);
                }
            }

            // Lowest first, so plan 0 is the ground everywhere and a bridge is plan 1
            // wherever there is ground beneath it. Where there is not, the bridge is
            // plan 0 -- which is correct: the plan index is a position in this cell's
            // own stack, and the rectangles are joined by height, not by index.
            std::sort(stack.begin(), stack.end(),
                      [](const Surface* a, const Surface* b) { return a->z < b->z; });

            for (size_t i = 0; i < stack.size(); ++i)
            {
                TilePlan& plan = plans[i];
                plan.region[static_cast<size_t>(cell)] = stack[i]->region;
                plan.layer[static_cast<size_t>(cell)] = stack[i]->layer;
                plan.z[static_cast<size_t>(cell)] = stack[i]->z;
                plan.area[static_cast<size_t>(cell)] = stack[i]->area;
            }
        }

        return plans;
    }

    TilePlan ReadTilePlan(const NavTile& tile)
    {
        TilePlan plan;
        plan.region.assign(static_cast<size_t>(SIDE) * SIDE, TilePlan::NO_REGION);
        plan.layer.assign(static_cast<size_t>(SIDE) * SIDE, 0);
        plan.z.assign(static_cast<size_t>(SIDE) * SIDE, 0.0f);
        plan.area.assign(static_cast<size_t>(SIDE) * SIDE, 0);

        std::vector<Surface> surfaces;

        for (int cell = 0; cell < SIDE * SIDE; ++cell)
        {
            tile.SurfacesAt(cell, surfaces);
            if (surfaces.empty())
            {
                continue;
            }

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
                plan.region[static_cast<size_t>(cell)] = pick->region;
                plan.layer[static_cast<size_t>(cell)] = pick->layer;
                plan.z[static_cast<size_t>(cell)] = pick->z;
                plan.area[static_cast<size_t>(cell)] = pick->area;
            }
        }

        return plan;
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
        return DecomposeTile(tile, ReadTilePlan(tile), nullptr);
    }

    std::vector<NavRect> DecomposeTile(const NavTile& tile, const TilePlan& plan,
                                       std::vector<int32_t>* cellToRect)
    {
        std::vector<uint8_t> taken(static_cast<size_t>(SIDE) * SIDE, 0);
        std::vector<NavRect> out;

        if (cellToRect)
        {
            cellToRect->assign(static_cast<size_t>(SIDE) * SIDE, -1);
        }

        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                const size_t seed = static_cast<size_t>(x) * SIDE + static_cast<size_t>(y);
                const uint16_t region = plan.region[seed];
                if (region == TilePlan::NO_REGION || taken[seed])
                {
                    continue;
                }

                // Growth stops where the AREA CLASS changes, and at nothing else about
                // the packed byte. A profile admits a mover to ground, to shallow water
                // or to neither, so a rectangle spanning both could not be tested at all
                // -- the shoreline is a rectangle boundary, which it should be anyway.
                //
                // The FLAGS in the same byte are a different matter, and comparing the
                // whole byte was measured at six times the geometry: 9,500,396
                // rectangles over map 0 against 1,480,966. CELL_BORDER is set on every
                // cell that touches an edge, so comparing it stops growth one cell into
                // every rectangle -- it marks the rim of the walkable set, and using it
                // as an identity fragments the set at exactly its own boundary.
                //
                // CELL_STEEP is kept, because it is a real difference in what crossing
                // costs and a rectangle that mixed steep and level ground would be
                // priced wrong either way round.
                const NavArea area = AreaOf(plan.area[seed]);
                const uint8_t steep = FlagsOf(plan.area[seed]) & CELL_STEEP;

                const auto same = [&](size_t at)
                {
                    return plan.region[at] == region && !taken[at] &&
                           AreaOf(plan.area[at]) == area &&
                           (FlagsOf(plan.area[at]) & CELL_STEEP) == steep;
                };

                // Along y first, because y is the contiguous index and a run of open
                // ground is a run in memory.
                int y1 = y;
                while (y1 + 1 < SIDE &&
                       same(static_cast<size_t>(x) * SIDE + static_cast<size_t>(y1 + 1)))
                {
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
                        wholeRow = same(static_cast<size_t>(x1 + 1) * SIDE +
                                        static_cast<size_t>(scan));
                    }

                    if (!wholeRow)
                    {
                        break;
                    }
                    ++x1;
                }

                NavRect rect;
                rect.x0 = static_cast<uint16_t>(x);
                rect.y0 = static_cast<uint16_t>(y);
                rect.x1 = static_cast<uint16_t>(x1);
                rect.y1 = static_cast<uint16_t>(y1);
                rect.region = region;
                rect.minZ = std::numeric_limits<float>::max();
                rect.maxZ = -std::numeric_limits<float>::max();
                rect.clearance = 0xFF;
                rect.area = PackArea(area, steep);
                rect.layer = plan.layerIndex;

                const int32_t index = static_cast<int32_t>(out.size());

                for (int ax = x; ax <= x1; ++ax)
                {
                    for (int ay = y; ay <= y1; ++ay)
                    {
                        const size_t at = static_cast<size_t>(ax) * SIDE +
                            static_cast<size_t>(ay);
                        taken[at] = 1;
                        if (cellToRect)
                        {
                            (*cellToRect)[at] = index;
                        }

                        const Surface s = tile.SurfaceAt(int(at), plan.layer[at]);
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
