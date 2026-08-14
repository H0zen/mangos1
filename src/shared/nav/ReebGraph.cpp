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

#include "nav/ReebGraph.hpp"

#include "nav/NavGrid.hpp"

#include <algorithm>
#include <numeric>

namespace Nav
{
    namespace
    {
        constexpr int SIDE = CELLS_PER_TILE;

        /// Union-find over cells, with the elder-rule tie-break the sweep needs: the
        /// root of a merged pair is always the component with the LOWER minimum, so the
        /// younger feature is the one that dies and persistence is measured against the
        /// right basin.
        class Components
        {
            public:
                explicit Components(size_t n) : m_parent(n), m_basin(n, UINT32_MAX)
                {
                    std::iota(m_parent.begin(), m_parent.end(), uint32_t(0));
                }

                uint32_t Find(uint32_t at)
                {
                    while (m_parent[at] != at)
                    {
                        m_parent[at] = m_parent[m_parent[at]];   // halve the path
                        at = m_parent[at];
                    }
                    return at;
                }

                void Attach(uint32_t child, uint32_t elder) { m_parent[child] = elder; }

                uint32_t& Basin(uint32_t root) { return m_basin[root]; }

            private:
                std::vector<uint32_t> m_parent;
                std::vector<uint32_t> m_basin;
        };

        const int DX[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
        const int DY[8] = {0, 0, -1, 1, -1, 1, -1, 1};
    }

    ReebGraph BuildReebGraph(const NavTile& tile, const TilePlan& plan,
                             float minPersistence)
    {
        ReebGraph graph;

        // Every walkable cell, ordered by height. The sweep IS this order: everything
        // below the current cell has already been added, so a neighbour is either part
        // of the surface so far or is still above and irrelevant.
        std::vector<uint32_t> order;
        order.reserve(size_t(SIDE) * SIDE / 4);

        for (int cell = 0; cell < SIDE * SIDE; ++cell)
        {
            if (plan.Walkable(cell))
            {
                order.push_back(uint32_t(cell));
            }
        }

        if (order.empty())
        {
            return graph;
        }

        std::sort(order.begin(), order.end(),
                  [&plan](uint32_t a, uint32_t b)
                  {
                      if (plan.z[a] != plan.z[b])
                      {
                          return plan.z[a] < plan.z[b];
                      }
                      return a < b;    // a total order, so the sweep is reproducible
                  });

        const auto world = [&tile](uint32_t cell, float& x, float& y)
        {
            x = CellCentre(GlobalCell(tile.TileX(), int(cell) / SIDE));
            y = CellCentre(GlobalCell(tile.TileY(), int(cell) % SIDE));
        };

        Components components(size_t(SIDE) * SIDE);
        std::vector<uint8_t> added(size_t(SIDE) * SIDE, 0);

        // The height each basin's floor sits at, so persistence is a subtraction.
        std::vector<float> basinFloor;

        for (uint32_t cell : order)
        {
            const int x = int(cell) / SIDE;
            const int y = int(cell) % SIDE;

            // Which components does this cell touch among what is already below it?
            uint32_t roots[8];
            int count = 0;

            for (int dir = 0; dir < 8; ++dir)
            {
                const int nx = x + DX[dir];
                const int ny = y + DY[dir];
                if (nx < 0 || nx >= SIDE || ny < 0 || ny >= SIDE)
                {
                    continue;
                }

                const uint32_t other = uint32_t(nx * SIDE + ny);
                if (!added[other])
                {
                    continue;
                }

                const uint32_t root = components.Find(other);
                bool seen = false;
                for (int i = 0; i < count && !seen; ++i)
                {
                    seen = roots[i] == root;
                }
                if (!seen)
                {
                    roots[count++] = root;
                }
            }

            added[cell] = 1;

            if (count == 0)
            {
                // Nothing below it: a new component is born here. A local minimum, and
                // the floor of a basin.
                CriticalPoint point;
                point.kind = CriticalKind::Minimum;
                point.cell = cell;
                point.z = plan.z[cell];
                world(cell, point.x, point.y);
                point.basinA = uint32_t(graph.basins.size());
                point.basinB = point.basinA;

                components.Basin(components.Find(cell)) = point.basinA;
                basinFloor.push_back(point.z);
                graph.basins.push_back(point);
                continue;
            }

            // Join this cell to the first component it touches, then fold the rest in.
            uint32_t keep = components.Find(roots[0]);
            components.Attach(components.Find(cell), keep);

            for (int i = 1; i < count; ++i)
            {
                uint32_t other = components.Find(roots[i]);
                if (other == keep)
                {
                    continue;
                }

                const uint32_t basinKeep = components.Basin(keep);
                const uint32_t basinOther = components.Basin(other);

                // THE ELDER RULE. Whichever basin has the deeper floor survives; the
                // younger one is drowned here, and the height from its floor to this
                // cell is how much of a feature it ever was.
                const float floorKeep = basinFloor[basinKeep];
                const float floorOther = basinFloor[basinOther];

                const bool keepIsElder = floorKeep <= floorOther;
                const uint32_t elder = keepIsElder ? keep : other;
                const uint32_t younger = keepIsElder ? other : keep;
                const uint32_t youngerBasin = keepIsElder ? basinOther : basinKeep;

                const float persistence = plan.z[cell] - basinFloor[youngerBasin];

                if (persistence >= minPersistence)
                {
                    // A pass worth the name: two basins that were separate up to here.
                    CriticalPoint point;
                    point.kind = CriticalKind::Saddle;
                    point.cell = cell;
                    point.z = plan.z[cell];
                    world(cell, point.x, point.y);
                    point.basinA = components.Basin(elder);
                    point.basinB = youngerBasin;
                    point.persistence = persistence;

                    ReebGraph::Arc arc;
                    arc.from = youngerBasin;
                    arc.to = uint32_t(graph.passes.size());
                    arc.rise = persistence;

                    graph.passes.push_back(point);
                    graph.arcs.push_back(arc);
                }

                components.Attach(younger, elder);
                components.Basin(elder) = components.Basin(elder);
                keep = elder;
            }
        }

        // The summits: cells with nothing above them in their own neighbourhood. Walked
        // after the sweep because a maximum is a statement about what never arrived,
        // and the sweep only ever sees what has.
        for (uint32_t cell : order)
        {
            const int x = int(cell) / SIDE;
            const int y = int(cell) % SIDE;
            bool highest = true;

            for (int dir = 0; dir < 8 && highest; ++dir)
            {
                const int nx = x + DX[dir];
                const int ny = y + DY[dir];
                if (nx < 0 || nx >= SIDE || ny < 0 || ny >= SIDE)
                {
                    continue;
                }

                const uint32_t other = uint32_t(nx * SIDE + ny);
                if (plan.Walkable(int(other)) && plan.z[other] > plan.z[cell])
                {
                    highest = false;
                }
            }

            if (!highest)
            {
                continue;
            }

            CriticalPoint point;
            point.kind = CriticalKind::Maximum;
            point.cell = cell;
            point.z = plan.z[cell];
            world(cell, point.x, point.y);
            point.basinA = components.Basin(components.Find(cell));
            point.basinB = point.basinA;
            graph.summits.push_back(point);
        }

        return graph;
    }
}
