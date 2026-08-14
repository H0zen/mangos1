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

#include "nav/MedialAxis.hpp"

#include "nav/NavGrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nav
{
    namespace
    {
        constexpr int SIDE = CELLS_PER_TILE;
        constexpr float INF = std::numeric_limits<float>::max();

        int LocalX(int cell) { return cell / SIDE; }
        int LocalY(int cell) { return cell % SIDE; }

        /**
         * @brief The one-dimensional squared distance transform of a sampled function.
         *
         * Felzenszwalb & Huttenlocher: the transform of a sampled function is the lower
         * envelope of the parabolas rooted at each sample, and that envelope can be
         * built in one left-to-right pass because the parabolas are all the same shape.
         * Each new parabola either replaces the last one on the envelope or is appended,
         * so every sample is pushed and popped at most once and the whole pass is linear.
         *
         * Doing this instead of a chamfer mask matters here and is not a flourish: a
         * chamfer approximation is several percent wrong on the diagonal, and the
         * quantity being computed is the one that decides whether a creature fits
         * through a gap. Several percent of a doorway is a creature that sticks.
         *
         * @param f       the sampled function, `n` entries
         * @param d       filled with the transform
         * @param arg     filled with the index of the sample each entry came from --
         *                the feature transform, and the annotation the ECM is named for
         * @param scratch three arrays of at least n + 1 entries, reused across rows
         */
        void Transform1D(const float* f, int n, float* d, int* arg, int* v, float* zEnv,
                         int* argIn)
        {
            int k = 0;
            v[0] = 0;
            zEnv[0] = -INF;
            zEnv[1] = INF;

            for (int q = 1; q < n; ++q)
            {
                if (f[q] >= INF)
                {
                    continue;   // no parabola from an empty sample
                }

                float s = 0.0f;
                while (true)
                {
                    const int p = v[k];
                    if (f[p] >= INF)
                    {
                        // The envelope holds only a placeholder; take it over outright.
                        s = -INF;
                        break;
                    }

                    s = ((f[q] + static_cast<float>(q) * static_cast<float>(q)) - (f[p] +
                        static_cast<float>(p) * static_cast<float>(p))) /
                        (2.0f * static_cast<float>(q) - 2.0f * static_cast<float>(p));

                    if (s <= zEnv[k] && k > 0)
                    {
                        --k;
                        continue;
                    }
                    break;
                }

                ++k;
                v[k] = q;
                zEnv[k] = s;
                zEnv[k + 1] = INF;
            }

            k = 0;
            for (int q = 0; q < n; ++q)
            {
                while (zEnv[k + 1] < static_cast<float>(q))
                {
                    ++k;
                }

                const int p = v[k];
                if (f[p] >= INF)
                {
                    d[q] = INF;
                    arg[q] = -1;
                    continue;
                }

                const float delta = static_cast<float>(q) - static_cast<float>(p);
                d[q] = delta * delta + f[p];
                arg[q] = argIn ? argIn[p] : p;
            }
        }
    }

    DistanceField BuildDistanceField(const NavTile& tile, const TilePlan& plan)
    {
        (void)tile;

        DistanceField field;
        field.distance.assign(static_cast<size_t>(SIDE) * SIDE, 0.0f);
        field.nearest.assign(static_cast<size_t>(SIDE) * SIDE, -1);

        // Squared distances throughout, and the square root only at the end: the
        // transform is defined on squares and taking the root per pass would both cost
        // more and lose the exactness the method was chosen for.
        std::vector<float> squared(static_cast<size_t>(SIDE) * SIDE, 0.0f);
        std::vector<int32_t> feature(static_cast<size_t>(SIDE) * SIDE, -1);

        // Named, and not `column(static_cast<size_t>(SIDE))`. That spelling is a function
        // declaration -- the most vexing parse -- and every use of it below then fails
        // with an error about subscripting a pointer to function, which says nothing
        // about the line that actually went wrong.
        const size_t side = static_cast<size_t>(SIDE);

        std::vector<float> column(side);
        std::vector<float> result(side);
        std::vector<int> argOut(side);
        std::vector<int> argIn(side);
        std::vector<int> v(side + 1);
        std::vector<float> zEnv(side + 2);

        // Pass one, along y. An obstacle cell is a zero of the field; everything else
        // starts at infinity and is pulled down by the envelope.
        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                const int cell = x * SIDE + y;
                column[static_cast<size_t>(y)] = plan.Walkable(cell) ? INF : 0.0f;
                argIn[static_cast<size_t>(y)] = y;
            }

            Transform1D(column.data(), SIDE, result.data(), argOut.data(), v.data(),
                        zEnv.data(), argIn.data());

            for (int y = 0; y < SIDE; ++y)
            {
                const int cell = x * SIDE + y;
                squared[static_cast<size_t>(cell)] = result[static_cast<size_t>(y)];
                feature[static_cast<size_t>(cell)] =
                    argOut[static_cast<size_t>(y)] < 0 ? -1 : static_cast<int32_t>(x *
                        SIDE + argOut[size_t(y)]);
            }
        }

        // Pass two, along x, over the result of pass one. The feature index carried
        // through is the obstacle CELL, not a coordinate on this axis, which is what
        // makes the two passes compose into a genuine two-dimensional feature transform.
        std::vector<int> featureIn(side);

        for (int y = 0; y < SIDE; ++y)
        {
            for (int x = 0; x < SIDE; ++x)
            {
                const int cell = x * SIDE + y;
                column[static_cast<size_t>(x)] = squared[static_cast<size_t>(cell)];
                featureIn[static_cast<size_t>(x)] = feature[static_cast<size_t>(cell)];
            }

            Transform1D(column.data(), SIDE, result.data(), argOut.data(), v.data(),
                        zEnv.data(), featureIn.data());

            for (int x = 0; x < SIDE; ++x)
            {
                const int cell = x * SIDE + y;
                const float d2 = result[static_cast<size_t>(x)];
                field.distance[static_cast<size_t>(cell)] =
                    d2 >= INF ? INF : std::sqrt(d2) * CELL_SIZE;
                field.nearest[static_cast<size_t>(cell)] = argOut[static_cast<size_t>(x)];
            }
        }

        return field;
    }

    std::vector<AxisVertex> BuildMedialAxis(const NavTile& tile, const TilePlan& plan,
                                            const DistanceField& field, float separation)
    {
        std::vector<AxisVertex> axis;

        // Which cells already carry a vertex, for the radius suppression below.
        std::vector<uint8_t> kept(static_cast<size_t>(SIDE) * SIDE, 0);

        const float separation2 = separation * separation;

        for (int x = 1; x < SIDE - 1; ++x)
        {
            for (int y = 1; y < SIDE - 1; ++y)
            {
                const int cell = x * SIDE + y;
                if (!plan.Walkable(cell))
                {
                    continue;
                }

                const int32_t mine = field.nearest[static_cast<size_t>(cell)];
                if (mine < 0)
                {
                    continue;   // nothing is near: an unbounded tile, no axis to speak of
                }

                const float mineX = static_cast<float>(LocalX(mine));
                const float mineY = static_cast<float>(LocalY(mine));

                // Equidistant from two pieces of boundary, and the two pieces are
                // genuinely apart. This is the definition, tested directly; a local
                // maximum of the distance field would be a heuristic for it and would
                // grow a spur off every notch a staircase boundary has.
                bool onAxis = false;
                for (int dx = -1; dx <= 1 && !onAxis; ++dx)
                {
                    for (int dy = -1; dy <= 1 && !onAxis; ++dy)
                    {
                        if (!dx && !dy)
                        {
                            continue;
                        }

                        const int other = (x + dx) * SIDE + (y + dy);
                        if (!plan.Walkable(other))
                        {
                            continue;
                        }

                        const int32_t theirs = field.nearest[static_cast<size_t>(other)];
                        if (theirs < 0 || theirs == mine)
                        {
                            continue;
                        }

                        const float ox = static_cast<float>(LocalX(theirs)) - mineX;
                        const float oy = static_cast<float>(LocalY(theirs)) - mineY;
                        const float apart = (ox * ox + oy * oy) * CELL_SIZE * CELL_SIZE;

                        // And this cell is at least as roomy as that neighbour, so a
                        // ridge two cells wide contributes one vertex rather than two
                        // facing each other.
                        onAxis = apart >= separation2 &&
                                 field.At(cell) >= field.At(other);
                    }
                }

                if (!onAxis)
                {
                    continue;
                }

                // A WEAK local maximum: no neighbour has strictly more room. Strict
                // would be wrong and the corridor case is why -- along a passage of
                // constant width the clearance on the centre line is constant, so no
                // cell is strictly above its neighbours and a strict test throws the
                // whole axis away. Breaking the tie by index instead keeps exactly one
                // vertex per plateau, which for a straight corridor is one vertex for
                // the entire corridor.
                bool crest = true;
                for (int dx = -1; dx <= 1 && crest; ++dx)
                {
                    for (int dy = -1; dy <= 1 && crest; ++dy)
                    {
                        if (!dx && !dy)
                        {
                            continue;
                        }

                        const int other = (x + dx) * SIDE + (y + dy);
                        crest = !plan.Walkable(other) ||
                                field.At(other) <= field.At(cell);
                    }
                }

                if (!crest)
                {
                    continue;
                }

                // Non-maximum suppression by RADIUS, and the radius is the room itself.
                // Two maximal disks closer together than one of their radii describe the
                // same passage, so keeping both stores the passage twice -- which is how
                // the axis came out at one vertex per 10.7 walkable cells, a field
                // wearing a structure's name.
                //
                // Scaling the radius with the clearance is what makes this a description
                // rather than a resampling: a doorway keeps its vertices close together
                // because its room changes quickly, and a hall keeps a handful because
                // nothing about it changes over fifty yards.
                const float reach = std::max(separation, field.At(cell) * 0.5f);
                const int span = std::max(1, static_cast<int>(reach / CELL_SIZE));

                bool crowded = false;
                for (int dx = -span; dx <= span && !crowded; ++dx)
                {
                    for (int dy = -span; dy <= span && !crowded; ++dy)
                    {
                        const int ax = x + dx;
                        const int ay = y + dy;
                        if (ax < 0 || ax >= SIDE || ay < 0 || ay >= SIDE)
                        {
                            continue;
                        }
                        crowded = kept[static_cast<size_t>(ax) * SIDE +
                                       static_cast<size_t>(ay)] != 0;
                    }
                }

                if (crowded)
                {
                    continue;
                }

                kept[static_cast<size_t>(cell)] = 1;

                AxisVertex vertex;
                vertex.cell = static_cast<uint32_t>(cell);
                vertex.region = plan.region[static_cast<size_t>(cell)];
                vertex.clearance = field.At(cell);
                vertex.x = CellCentre(GlobalCell(tile.TileX(), x));
                vertex.y = CellCentre(GlobalCell(tile.TileY(), y));
                vertex.obstacleX = CellCentre(GlobalCell(tile.TileX(), LocalX(mine)));
                vertex.obstacleY = CellCentre(GlobalCell(tile.TileY(), LocalY(mine)));
                axis.push_back(vertex);
            }
        }

        return axis;
    }

    float ClearanceAt(const NavTile& tile, const DistanceField& field, float x, float y)
    {
        const int gx = CellIndex(x);
        const int gy = CellIndex(y);
        if (TileOfCell(gx) != tile.TileX() || TileOfCell(gy) != tile.TileY())
        {
            return -1.0f;
        }

        const int lx = LocalOfCell(gx);
        const int ly = LocalOfCell(gy);
        const float here = field.At(lx * SIDE + ly);
        if (here >= INF)
        {
            return here;
        }

        // Bilinear against the three neighbours towards the sample point, so the answer
        // moves continuously rather than stepping at every cell rim. Clamped at the tile
        // edge, where there is nothing to interpolate towards.
        const float fx = CellCoord(x) - std::floor(CellCoord(x));
        const float fy = CellCoord(y) - std::floor(CellCoord(y));

        const int nx = std::min(lx + 1, SIDE - 1);
        const int ny = std::min(ly + 1, SIDE - 1);

        const float a = field.At(lx * SIDE + ly);
        const float b = field.At(nx * SIDE + ly);
        const float c = field.At(lx * SIDE + ny);
        const float d = field.At(nx * SIDE + ny);

        if (a >= INF || b >= INF || c >= INF || d >= INF)
        {
            return here;
        }

        const float top = a * (1.0f - fx) + b * fx;
        const float bottom = c * (1.0f - fx) + d * fx;
        return top * (1.0f - fy) + bottom * fy;
    }
}
