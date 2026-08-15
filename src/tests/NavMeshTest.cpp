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
#include "nav/NavMeshIO.hpp"
#include "nav/NavTile.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
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

    /// Symmetry is a statement about openings INSIDE the tile. A rim run has nothing on
    /// the other side of it that this file knows about -- what lies there is matched by
    /// the store from the neighbour's own mesh -- so it has no mirror to look for, and
    /// looking for one means indexing the rectangle table with Portal::OUTSIDE.
    bool Symmetric(const Nav::TileMesh& mesh)
    {
        for (const Nav::Portal& p : mesh.portals)
        {
            if (p.LeavesTheTile())
            {
                continue;
            }

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

    /// Two mouths facing each other across the drop, and the hand-authored jump between
    /// them. Authored on the TILE, as `offmesh.txt` gives it; what the mesh does with it
    /// is resolve each mouth to the area it stands in.
    void AuthorALink(Nav::NavTile& tile, int fromX, int toX)
    {
        const auto mouth = [&tile](int localX, float z, uint16_t region)
        {
            Nav::Gateway gate;
            gate.side = Nav::NavTile::SIDE_LINK;
            gate.region = region;
            gate.width = 4.0f;
            gate.x = Nav::CellCentre(Nav::GlobalCell(tile.TileX(), localX));
            gate.y = Nav::CellCentre(Nav::GlobalCell(tile.TileY(), 128));
            gate.z = z;
            gate.firstZ = z;
            gate.lastZ = z;
            gate.cell = static_cast<uint32_t>(localX * SIDE + 128);
            return gate;
        };

        tile.MutableGateways().push_back(mouth(fromX, 0.0f, 0));
        tile.MutableGateways().push_back(mouth(toX, -30.0f, 1));

        Nav::Link link;
        link.fromGate = 0;
        link.toGate = 1;
        link.cost = 12.0f;
        link.bidirectional = true;
        tile.MutableLinks().push_back(link);
    }

    size_t RimPortals(const Nav::TileMesh& mesh, uint8_t side)
    {
        size_t count = 0;
        for (const Nav::Portal& p : mesh.portals)
        {
            if (p.LeavesTheTile() && p.side == side)
            {
                ++count;
            }
        }
        return count;
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
        CHECK(p.lo <= p.hi);
        if (!p.LeavesTheTile())
        {
            CHECK(p.neighbour < mesh.rects.size());
        }
    }
}

// Ground that reaches the tile's edge opens onto whatever is past it, on all four
// sides. That run is what replaces the baked gateway, and the store is what finds out
// into what -- so what is asserted here is that the run EXISTS and carries the three
// things a match needs, not what it connects to.
TEST(NavMesh_GroundAtTheEdgeOpensOffTheTile)
{
    Nav::NavTile tile;
    Paint(tile, HoledFloor());

    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    for (uint8_t side = 0; side < 4; ++side)
    {
        CHECK(RimPortals(mesh, side) > 0);
    }

    size_t rim = 0;
    for (const Nav::Portal& p : mesh.portals)
    {
        if (!p.LeavesTheTile())
        {
            continue;
        }

        ++rim;
        CHECK(p.clearance > 0);
        CHECK(std::fabs(p.loZ) < 0.5f);   // the floor was painted level
        CHECK(std::fabs(p.hiZ) < 0.5f);
    }

    CHECK(rim >= 4);
}

// Two shelves thirty yards apart still both reach the tile's edge, so both open off it
// -- being unreachable from each other says nothing about being reachable from the
// neighbouring tile, and a mesh that conflated the two would seal a map's seams.
TEST(NavMesh_ARimRunIsNotAboutTheOtherHalfOfTheTile)
{
    Nav::NavTile tile;
    Paint(tile, TwoShelves());

    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);

    CHECK_EQ(mesh.portals.size(), RimPortals(mesh, 0) + RimPortals(mesh, 1) +
                                      RimPortals(mesh, 2) + RimPortals(mesh, 3));
    CHECK(RimPortals(mesh, Nav::SIDE_MINUS_X) > 0);
    CHECK(RimPortals(mesh, Nav::SIDE_PLUS_X) > 0);
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

    // Not "no portals at all" any more: both shelves reach the tile's edge and open off
    // it. What must not exist is an opening between THEM.
    size_t inward = 0;
    for (const Nav::Portal& p : mesh.portals)
    {
        if (!p.LeavesTheTile())
        {
            ++inward;
        }
    }
    CHECK_EQ(inward, size_t(0));
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
        // Rim runs included: a doorway off the edge of a tile is measured the same way
        // as one inside it, and the store compares two of them against each other.
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        Nav::PortalSegment(tile, mesh, p, ax, ay, bx, by);

        const float length = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        const float expected = float(p.Cells()) * Nav::CELL_SIZE;
        CHECK(std::fabs(length - expected) < 0.01f);
    }
}

// A hand-authored jump becomes an edge between two AREAS. The tile carries it as a pair
// of gateway mouths, which is what the bake authors; what the mesh has to do is find the
// rectangle under each mouth -- by height, or a link authored on a dock resolves onto the
// water underneath it.
TEST(NavMesh_ALinkJoinsTheAreasItsMouthsStandIn)
{
    Nav::NavTile tile;
    Paint(tile, TwoShelves());
    AuthorALink(tile, 100, 400);

    const Nav::TileMesh mesh = Nav::BuildTileMesh(tile);
    REQUIRE(mesh.links.size() == static_cast<size_t>(1));

    const Nav::MeshLink& link = mesh.links[0];
    CHECK(link.fromRect < mesh.rects.size());
    CHECK(link.toRect < mesh.rects.size());
    CHECK(link.fromRect != link.toRect);

    // Each mouth landed on its own shelf, and the shelves are separate regions -- which
    // is the whole reason the link is there.
    CHECK(mesh.rects[link.fromRect].region != mesh.rects[link.toRect].region);

    // The mouths keep the positions they were authored at. A link is a place, not the
    // middle of the area the place belongs to.
    CHECK(std::fabs(link.fromZ - 0.0f) < 0.01f);
    CHECK(std::fabs(link.toZ + 30.0f) < 0.01f);
    CHECK(link.clearance > 0);
    CHECK(link.bidirectional != 0);
}

// The cache has to survive its own file. Two things are asserted that a reader has got
// wrong before: a rim portal names Portal::OUTSIDE and is NOT an out-of-range index --
// reading it as one threw away the cache for every tile whose ground reaches an edge, so
// the baked mesh was silently rebuilt on the map's tick and never once used -- and the
// links come back at all, which a reader written before them would drop in silence.
TEST(NavMeshIO_ARoundTripKeepsRimPortalsAndLinks)
{
    Nav::NavTile tile;
    Paint(tile, TwoShelves());
    AuthorALink(tile, 100, 400);

    Nav::TileGeometry written;
    written.mesh = Nav::BuildTileMesh(tile);
    REQUIRE(!written.mesh.rects.empty());
    REQUIRE(written.mesh.links.size() == static_cast<size_t>(1));
    REQUIRE(RimPortals(written.mesh, Nav::SIDE_MINUS_X) > 0);

    const std::string path = "./NavMeshIO_roundtrip.mesh";
    REQUIRE(Nav::WriteTileGeometry(path, 4242, 32, 32, written));

    // A file from another map or another tile is refused rather than read as if it
    // described this one.
    Nav::TileGeometry wrong;
    CHECK(!Nav::ReadTileGeometry(path, 4243, 32, 32, wrong));
    CHECK(!Nav::ReadTileGeometry(path, 4242, 33, 32, wrong));

    Nav::TileGeometry read;
    const bool ok = Nav::ReadTileGeometry(path, 4242, 32, 32, read);
    std::remove(path.c_str());
    REQUIRE(ok);

    CHECK_EQ(read.mesh.rects.size(), written.mesh.rects.size());
    CHECK_EQ(read.mesh.portals.size(), written.mesh.portals.size());
    CHECK_EQ(read.mesh.first.size(), written.mesh.first.size());
    CHECK_EQ(read.mesh.heights.size(), written.mesh.heights.size());
    REQUIRE(read.mesh.links.size() == static_cast<size_t>(1));

    CHECK_EQ(read.mesh.links[0].fromRect, written.mesh.links[0].fromRect);
    CHECK_EQ(read.mesh.links[0].toRect, written.mesh.links[0].toRect);
    CHECK(std::fabs(read.mesh.links[0].cost - written.mesh.links[0].cost) < 0.01f);
    CHECK(std::fabs(read.mesh.links[0].toX - written.mesh.links[0].toX) < 0.01f);

    // Every rim run came back as a rim run, and no reader turned one into an index.
    for (uint8_t side = 0; side < 4; ++side)
    {
        CHECK_EQ(RimPortals(read.mesh, side), RimPortals(written.mesh, side));
    }
}
