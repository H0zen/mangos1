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

// THE TRANSFORM IS EXACT OR IT IS NOT WORTH HAVING.
//
// A chamfer distance mask is several percent wrong on the diagonal and nobody notices,
// because the number it produces looks reasonable. The number decides whether a creature
// fits through a gap, so "looks reasonable" is not a standard. Every case here checks
// against the closed-form Euclidean answer, including the diagonal, which is exactly
// where an approximation shows.
//
// Then the axis itself: down the middle of a corridor, with the clearance of a corridor,
// which is the property the Explicit Corridor Map exists to provide.

#include "TestHarness.h"

#include "nav/MedialAxis.hpp"
#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"
#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"

#include <cmath>
#include <cstdint>

namespace
{
    constexpr int SIDE = Nav::CELLS_PER_TILE;

    template <typename Shape>
    void Paint(Nav::NavTile& tile, const Shape& shape)
    {
        tile.Reset(32, 32, -10.0f);
        tile.SetParams(Nav::TileParams());

        Nav::Region region;
        region.areas = Nav::AreaBit(Nav::NavArea::Ground);
        region.maxClearance = 8.0f;
        region.minZ = -1.0f;
        region.maxZ = 1.0f;
        tile.MutableRegions().assign(1, region);

        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                if (!shape(x, y))
                {
                    continue;
                }

                tile.SetCell(x * SIDE + y, Nav::QuantiseZ(0.0f, tile.BaseZ()),
                             Nav::PackArea(Nav::NavArea::Ground, 0),
                             Nav::QuantiseClearance(8.0f), 0);
            }
        }
    }

    /// Open ground with one blocked cell in the middle of it: the field around it is a
    /// cone whose value at any cell is the plain Euclidean distance to that cell.
    struct OnePillar
    {
        static constexpr int PX = 256;
        static constexpr int PY = 256;
        bool operator()(int x, int y) const { return !(x == PX && y == PY); }
    };

    /// A corridor 41 cells wide running the length of the tile, walls either side.
    struct Corridor
    {
        static constexpr int LOW = 200;
        static constexpr int HIGH = 240;   // inclusive, so 41 cells
        bool operator()(int x, int) const { return x >= LOW && x <= HIGH; }
    };
}

// The distance transform against the closed form, on the axes and on the diagonal. A
// chamfer mask passes the first two and fails the third by about six percent.
TEST(MedialAxis_TheDistanceTransformIsExact)
{
    Nav::NavTile tile;
    Paint(tile, OnePillar());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::DistanceField field = Nav::BuildDistanceField(tile, plan);

    struct Probe { int dx; int dy; };
    const Probe probes[] = {{10, 0}, {0, 10}, {7, 7}, {12, 5}, {3, 40}};

    for (const Probe& probe : probes)
    {
        const int x = OnePillar::PX + probe.dx;
        const int y = OnePillar::PY + probe.dy;
        const float expected =
            std::sqrt(float(probe.dx * probe.dx + probe.dy * probe.dy)) * Nav::CELL_SIZE;

        const float got = field.At(x * SIDE + y);
        CHECK(std::fabs(got - expected) < 0.01f);
    }
}

// And the feature transform: every cell knows WHICH obstacle is nearest, which is the
// annotation that makes the axis extractable by definition rather than by threshold.
TEST(MedialAxis_EveryCellKnowsItsNearestObstacle)
{
    Nav::NavTile tile;
    Paint(tile, OnePillar());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::DistanceField field = Nav::BuildDistanceField(tile, plan);

    const int32_t pillar = OnePillar::PX * SIDE + OnePillar::PY;
    const int probes[][2] = {{266, 256}, {256, 266}, {263, 263}, {200, 300}};

    for (const auto& probe : probes)
    {
        CHECK_EQ(field.nearest[size_t(probe[0] * SIDE + probe[1])], pillar);
    }
}

// The axis of a corridor is its centre line, and the clearance there is half its width.
// This is the whole promise of the structure: one number, at one place, answering for
// every mover that might want to pass.
TEST(MedialAxis_TheAxisOfACorridorIsItsCentreLine)
{
    Nav::NavTile tile;
    Paint(tile, Corridor());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::DistanceField field = Nav::BuildDistanceField(tile, plan);
    const std::vector<Nav::AxisVertex> axis =
        Nav::BuildMedialAxis(tile, plan, field, 2.0f);

    REQUIRE(!axis.empty());

    const int middle = (Corridor::LOW + Corridor::HIGH) / 2;
    const float halfWidth = 21.0f * Nav::CELL_SIZE;   // centre to the wall cell

    size_t offCentre = 0;
    for (const Nav::AxisVertex& vertex : axis)
    {
        const int x = int(vertex.cell) / SIDE;
        if (std::abs(x - middle) > 1)
        {
            ++offCentre;
        }
        CHECK(vertex.clearance <= halfWidth + 0.01f);
    }

    CHECK_EQ(offCentre, size_t(0));

    // And the clearance on the centre line really is half the corridor.
    CHECK(std::fabs(axis.front().clearance - halfWidth) < Nav::CELL_SIZE + 0.01f);
}

// The point query reads the field continuously, so a mover crossing a cell rim does not
// see its room jump.
TEST(MedialAxis_ClearanceAtIsContinuousAcrossACellRim)
{
    Nav::NavTile tile;
    Paint(tile, Corridor());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::DistanceField field = Nav::BuildDistanceField(tile, plan);

    const int centre = (Corridor::LOW + Corridor::HIGH) / 2;
    const float y = Nav::CellCentre(Nav::GlobalCell(tile.TileY(), 300));

    float previous = -1.0f;
    for (int step = 0; step < 40; ++step)
    {
        const float x = Nav::CellCentre(Nav::GlobalCell(tile.TileX(), centre)) -
                        float(step) * Nav::CELL_SIZE * 0.25f;
        const float here = Nav::ClearanceAt(tile, field, x, y);
        REQUIRE(here >= 0.0f);

        if (previous >= 0.0f)
        {
            // A quarter-cell step may not change the answer by more than a quarter of a
            // cell: the field is 1-Lipschitz, and so must the read of it be.
            CHECK(std::fabs(here - previous) <= Nav::CELL_SIZE * 0.25f + 0.01f);
        }
        previous = here;
    }
}
