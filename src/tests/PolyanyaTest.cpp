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
