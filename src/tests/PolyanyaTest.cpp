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

// OPTIMAL MEANS A NUMBER, SO THE ASSERTIONS ARE NUMBERS.
//
// The claim being made for this search is not "it finds a path" -- a graph search does
// that -- it is that the path is the Euclidean shortest one over the mesh, with no
// smoothing pass. That claim is only worth anything where the true length is known in
// closed form, so every case here is a shape whose answer can be written down:
//
//   open ground   the straight line, to the yard
//   round a wall  the taut path over the corner, by Pythagoras
//   no way out    nothing, and quickly
//
// A funnel-based path would pass the first and fail the second by exactly the amount
// the corridor cost it.

#include "TestHarness.h"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"
#include "nav/NavMesh.hpp"
#include "nav/NavTile.hpp"
#include "nav/Polyanya.hpp"

#include <cmath>
#include <cstdint>

namespace
{
    constexpr int SIDE = Nav::CELLS_PER_TILE;

    template <typename Shape>
    void Paint(Nav::NavTile& tile, const Shape& shape)
    {
        tile.Reset(32, 32, -100.0f);
        tile.SetParams(Nav::TileParams());

        Nav::Region region;
        region.areas = Nav::AreaBit(Nav::NavArea::Ground);
        region.maxClearance = 4.0f;
        region.minZ = -1.0f;
        region.maxZ = 1.0f;
        tile.MutableRegions().assign(size_t(shape.Regions()), region);

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
                             Nav::QuantiseClearance(4.0f), 0);
            }
        }
    }

    struct OpenGround
    {
        bool operator()(int, int) const { return true; }
        int Regions() const { return 1; }
    };

    /// A wall along x = 256 from y = 0 up to y = 300, leaving a way round its tip.
    /// The shortest route is start -> the tip -> end, and the tip is a mesh corner.
    struct WallWithATip
    {
        bool operator()(int x, int y) const { return !(x == 256 && y < 300); }
        int Regions() const { return 1; }
    };

    /// The same wall, all the way across. Nothing joins the two halves.
    struct SolidWall
    {
        bool operator()(int x, int) const { return x != 256; }
        int Regions() const { return 2; }
    };

    /// World position of the centre of an in-tile cell.
    void CellWorld(const Nav::NavTile& tile, int lx, int ly, float& x, float& y)
    {
        x = Nav::CellCentre(Nav::GlobalCell(tile.TileX(), lx));
        y = Nav::CellCentre(Nav::GlobalCell(tile.TileY(), ly));
    }

    /// FOUR THIN WALLS THAT BLOCK NOTHING, and the shape that found a real defect.
    ///
    /// The diagonal from corner to corner misses every one of them, so the answer is the
    /// straight line and two points. It was neither: the search bent once, at the end of
    /// a portal, for 696.4082 against the straight 695.3217 -- a yard longer through
    /// completely open ground.
    ///
    /// Found by generating random obstacle fields, testing the one property that cannot
    /// lie (running the same query again with the first answer as an upper bound may
    /// only remove areas, so the second answer can never be SHORTER), and then dropping
    /// walls one at a time while it still broke. Nine walls came down to four.
    struct FourThinWalls
    {
        bool operator()(int x, int y) const
        {
            return !((x >= 82 && x <= 84 && y >= 343 && y <= 372) ||
                     (x >= 129 && x <= 149 && y >= 78 && y <= 79) ||
                     (x >= 47 && x <= 105 && y >= 152 && y <= 155) ||
                     (x >= 200 && x <= 202 && y >= 339 && y <= 388));
        }
        int Regions() const { return 1; }
    };

    float Straight(float x0, float y0, float x1, float y1)
    {
        return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    }
}

// Nothing in the way: the answer is the straight line and the path is two points. A
// search that returned the cell route would be some percent longer and would have
// dozens of points.
TEST(Polyanya_OpenGroundIsTheStraightLine)
{
    Nav::NavTile tile;
    Paint(tile, OpenGround());
    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    Nav::MeshQuery query;
    CellWorld(tile, 20, 20, query.startX, query.startY);
    CellWorld(tile, 480, 400, query.endX, query.endY);

    const Nav::MeshPath path = Nav::FindMeshPath(tile, mesh, query);
    REQUIRE(path.found);

    const float direct = Straight(query.startX, query.startY, query.endX, query.endY);
    CHECK(std::fabs(path.length - direct) < 0.05f);
    CHECK_EQ(path.points.size(), size_t(2));
}

// Round a wall's tip. The shortest route bends exactly once, at the tip, and its length
// is the two hypotenuses -- which is what "optimal, no smoothing" has to mean.
TEST(Polyanya_RoundACornerIsTaut)
{
    Nav::NavTile tile;
    Paint(tile, WallWithATip());
    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    Nav::MeshQuery query;
    CellWorld(tile, 200, 100, query.startX, query.startY);
    CellWorld(tile, 320, 100, query.endX, query.endY);

    const Nav::MeshPath path = Nav::FindMeshPath(tile, mesh, query);
    REQUIRE(path.found);

    // The tip is the cell just past the end of the wall.
    float tipX = 0.0f, tipY = 0.0f;
    CellWorld(tile, 256, 300, tipX, tipY);

    const float taut = Straight(query.startX, query.startY, tipX, tipY) +
                       Straight(tipX, tipY, query.endX, query.endY);

    // Within a cell of the ideal: the mesh's corner is a cell rim, not a mathematical
    // point, so the bend can only be placed to that resolution.
    CHECK(path.length <= taut + Nav::CELL_SIZE * 2.0f);
    CHECK(path.length >= Straight(query.startX, query.startY, query.endX, query.endY));

    // And it is a bend, not a staircase.
    CHECK(path.points.size() <= 4);
}

// Two halves that never meet. The search must say so rather than return the nearest
// thing it found, and must not have to exhaust its budget to do it.
TEST(Polyanya_ASolidWallIsUnroutable)
{
    Nav::NavTile tile;
    Paint(tile, SolidWall());
    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    Nav::MeshQuery query;
    CellWorld(tile, 100, 200, query.startX, query.startY);
    CellWorld(tile, 400, 200, query.endX, query.endY);

    const Nav::MeshPath path = Nav::FindMeshPath(tile, mesh, query);
    CHECK(!path.found);
    CHECK(path.expansions < query.maxExpansions);
}

// A mover wider than an area's recorded clearance may not enter it. One bake, every
// creature size -- the property the cell grid was built for and the polygon layer has
// to keep.
TEST(Polyanya_ClearanceRefusesAMoverThatDoesNotFit)
{
    Nav::NavTile tile;
    Paint(tile, OpenGround());
    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    Nav::MeshQuery query;
    CellWorld(tile, 20, 20, query.startX, query.startY);
    CellWorld(tile, 480, 400, query.endX, query.endY);
    // Far past the 4 yards the ground was painted with.
    query.profile.radius = 40.0f;

    const Nav::MeshPath path = Nav::FindMeshPath(tile, mesh, query);
    CHECK(!path.found);
}

// The heuristic must never claim a path is longer than it is.
//
// `Heuristic` asks whether the target is visible from the root straight through the
// interval, and if it is, the estimate is the straight-line distance. That test compared
// a parameter it had computed with the wrong sign -- one negation where the identical
// arithmetic twenty lines away has two -- so it answered "not visible" for crossings
// that plainly were, and fell through to "the path must round an endpoint".
//
// Rounding an endpoint is LONGER than going straight through. An overestimating
// heuristic is inadmissible, and an inadmissible heuristic makes A* return whatever it
// reaches first. This shape is the smallest one on which that showed: the diagonal is
// unobstructed, and the search bent anyway.
//
// The expansion count is asserted too, and deliberately loosely. It fell from 30 to 7
// with the sign corrected, and a regression that reintroduced the overestimate would
// blow through 15 long before it changed the length.
TEST(Polyanya_AnUnobstructedDiagonalIsNotBent)
{
    Nav::NavTile tile;
    Paint(tile, FourThinWalls());
    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    Nav::MeshQuery query;
    CellWorld(tile, 20, 20, query.startX, query.startY);
    CellWorld(tile, SIDE - 20, SIDE - 20, query.endX, query.endY);

    const Nav::MeshPath path = Nav::FindMeshPath(tile, mesh, query);
    REQUIRE(path.found);
    CHECK(!path.exhausted);

    const float direct = Straight(query.startX, query.startY, query.endX, query.endY);
    CHECK(std::fabs(path.length - direct) < 0.01f);
    CHECK_EQ(path.points.size(), size_t(2));
    CHECK(path.expansions < 15u);
}

// The property the defect was found with, kept as a test in its own right.
//
// Running a query again with its own answer as an upper bound can only remove areas from
// the search -- Nav::MeshBound discards those that provably cannot carry a shorter route
// -- so the second answer may be equal but never shorter. When it came back shorter, the
// first search had not been optimal, and that is what this asserts on a shape where it
// used to fail by a yard.
TEST(Polyanya_ABoundNeverImprovesAnOptimalAnswer)
{
    Nav::NavTile tile;
    Paint(tile, FourThinWalls());
    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    Nav::MeshQuery query;
    CellWorld(tile, 20, 20, query.startX, query.startY);
    CellWorld(tile, SIDE - 20, SIDE - 20, query.endX, query.endY);

    const Nav::MeshPath first = Nav::FindMeshPath(tile, mesh, query);
    REQUIRE(first.found);

    query.upperBound = first.length;
    const Nav::MeshPath second = Nav::FindMeshPath(tile, mesh, query);
    REQUIRE(second.found);

    CHECK(second.length >= first.length - 0.01f);
}
