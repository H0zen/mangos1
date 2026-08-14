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

// A MESH IS THE OPENINGS, NOT THE AREAS. The rectangles were checked against the cells
// in NavPolygonTest; what is checked here is the part a search actually moves through.
//
// Three properties, and each one is a way a mesh can be wrong while looking right:
//
//   - symmetry. A run from A to B must have an identical run from B to A, or a search
//     finds a route one way and not back.
//   - no opening where the ground does not join. Two rectangles can share a hundred
//     cells of border and be joined along twenty of them; a mesh that recorded the whole
//     shared edge would walk routes off a ledge.
//   - the segment spans the opening, rim to rim. Measuring centre to centre shortens
//     every doorway in the world by one cell, and nothing shows it until a route refuses
//     a gap the mover fits through.

#include "TestHarness.h"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"
#include "nav/NavMesh.hpp"
#include "nav/NavTile.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    constexpr int SIDE = Nav::CELLS_PER_TILE;

    /// Paint a tile: `height(x, y)` returns the floor height, or nothing for a hole.
    /// One region per distinct returned region id.
    template <typename Shape>
    void Paint(Nav::NavTile& tile, const Shape& shape)
    {
        tile.Reset(32, 32, -100.0f);

        Nav::TileParams params;
        tile.SetParams(params);

        Nav::Region region;
        region.areas = Nav::AreaBit(Nav::NavArea::Ground);
        region.maxClearance = 4.0f;
        region.minZ = -50.0f;
        region.maxZ = 50.0f;
        tile.MutableRegions().assign(size_t(shape.Regions()), region);

        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                float z = 0.0f;
                const int owner = shape(x, y, z);
                if (owner < 0)
                {
                    continue;
                }

                tile.SetCell(x * SIDE + y, Nav::QuantiseZ(z, tile.BaseZ()),
                             Nav::PackArea(Nav::NavArea::Ground, 0),
                             Nav::QuantiseClearance(4.0f), uint16_t(owner));
            }
        }
    }

    /// Flat ground with a hole punched through the middle of it, so the greedy scan is
    /// forced to emit several rectangles that then have to find each other.
    struct HoledFloor
    {
        int operator()(int x, int y, float& z) const
        {
            z = 0.0f;
            const bool hole = x >= 200 && x < 300 && y >= 200 && y < 300;
            return hole ? -1 : 0;
        }
        int Regions() const { return 1; }
    };

    /// Two shelves at different heights, meeting along x = 256. The drop is far past
    /// any climb limit, so the two are separate regions and nothing joins them.
    struct TwoShelves
    {
        int operator()(int x, int, float& z) const
        {
            z = x < 256 ? 0.0f : -30.0f;
            return x < 256 ? 0 : 1;
        }
        int Regions() const { return 2; }
    };

    bool Symmetric(const Nav::TileMesh& mesh)
    {
        for (const Nav::Portal& p : mesh.portals)
        {
            bool mirrored = false;
            for (uint32_t i = mesh.first[p.neighbour];
                 i < mesh.first[p.neighbour + 1] && !mirrored; ++i)
            {
                const Nav::Portal& q = mesh.portals[i];
                mirrored = q.neighbour == p.rect && q.lo == p.lo && q.hi == p.hi;
            }

            if (!mirrored)
            {
                return false;
            }
        }

        return true;
    }
}

// Open ground with a hole: the rectangles are several, every one of them reachable from
// its neighbours, and every opening reported from both sides.
TEST(NavMesh_OpeningsAreSymmetric)
{
    Nav::NavTile tile;
    Paint(tile, HoledFloor());

    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);
    REQUIRE(mesh.rects.size() > 1);
    CHECK(!mesh.portals.empty());
    CHECK(Symmetric(mesh));

    // Nothing may open onto itself, and every neighbour index must exist.
    for (const Nav::Portal& p : mesh.portals)
    {
        CHECK(p.rect != p.neighbour);
        CHECK(p.neighbour < mesh.rects.size());
        CHECK(p.lo <= p.hi);
    }
}

// A thirty-yard drop is not a step. The two shelves touch along their whole length and
// the mesh must contain no opening at all between them -- this is the property that
// stops a route walking off a ledge because two areas happened to be adjacent in plan.
TEST(NavMesh_AdjacentIsNotJoined)
{
    Nav::NavTile tile;
    Paint(tile, TwoShelves());

    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);
    REQUIRE(mesh.rects.size() == 2);
    CHECK(Symmetric(mesh));
    CHECK_EQ(mesh.portals.size(), size_t(0));
}

// The opening between the two halves of a split floor spans the cells it covers, rim to
// rim: n cells of opening are n * CELL_SIZE of segment, not (n - 1).
TEST(NavMesh_ASegmentSpansTheWholeOpening)
{
    Nav::NavTile tile;
    Paint(tile, HoledFloor());

    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);
    REQUIRE(!mesh.portals.empty());

    for (const Nav::Portal& p : mesh.portals)
    {
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        Nav::PortalSegment(tile, mesh, p, ax, ay, bx, by);

        const float length = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        const float expected = float(p.Cells()) * Nav::CELL_SIZE;
        CHECK(std::fabs(length - expected) < 0.01f);
    }
}
