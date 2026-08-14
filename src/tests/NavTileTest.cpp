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

// A baked tile: what it holds, and what survives a trip through its own file.
//
// The round trip is the case that matters. Writer and reader live in one file so a
// section cannot be added to one and forgotten in the other, but "cannot be forgotten"
// is a claim about attention, and this is the thing that makes it a claim about bytes.

#include "TestHarness.h"

#include "nav/NavTile.hpp"
#include "nav/NavTileIO.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    /// A tile with a floor, a wall down the middle, and a balcony over part of it.
    Nav::NavTile MakeTile()
    {
        Nav::NavTile tile;
        tile.Reset(30, 41, -50.0f);

        Nav::TileParams params;
        params.maxClimb = 1.25f;
        params.agentHeight = 2.1f;
        tile.SetParams(params);

        Nav::Region ground;
        ground.areas = Nav::AreaBit(Nav::NavArea::Ground);
        ground.maxClearance = 8.0f;
        ground.minZ = -20.0f;
        ground.maxZ = -20.0f;

        Nav::Region balcony = ground;
        balcony.minZ = -12.0f;
        balcony.maxZ = -12.0f;

        tile.MutableRegions().push_back(ground);
        tile.MutableRegions().push_back(balcony);

        for (int lx = 0; lx < Nav::CELLS_PER_TILE; ++lx)
        {
            for (int ly = 0; ly < Nav::CELLS_PER_TILE; ++ly)
            {
                const int inTile = lx * Nav::CELLS_PER_TILE + ly;

                // A wall: one column of cells nothing may stand on.
                if (lx == 256)
                {
                    continue;
                }

                tile.SetCell(inTile, Nav::QuantiseZ(-20.0f, tile.BaseZ()),
                             Nav::PackArea(Nav::NavArea::Ground, 0),
                             Nav::QuantiseClearance(4.0f), 0);

                // A balcony over the first few rows: a second surface in the same cell.
                if (lx < 4)
                {
                    Nav::StackedLayer layer;
                    layer.cell = uint32_t(inTile);
                    layer.z = Nav::QuantiseZ(-12.0f, tile.BaseZ());
                    layer.region = 1;
                    layer.area = Nav::PackArea(Nav::NavArea::Ground, 0);
                    layer.clearance = Nav::QuantiseClearance(2.0f);
                    tile.AddStacked(layer);

                    tile.MutableAreaPlane()[size_t(inTile)] |= Nav::CELL_STACKED;
                }
            }
        }

        tile.SortStacked();

        Nav::Gateway gate;
        gate.side = Nav::NavTile::SIDE_LOW_Y;
        gate.first = 0;
        gate.last = 255;
        gate.region = 0;
        gate.firstZ = -20.0f;
        gate.lastZ = -20.0f;
        gate.width = 4.0f;
        gate.x = 1.0f;
        gate.y = 2.0f;
        gate.z = -20.0f;
        tile.MutableGateways().push_back(gate);

        Nav::Gateway other = gate;
        other.side = Nav::NavTile::SIDE_HIGH_Y;
        other.cell = uint32_t(300 * Nav::CELLS_PER_TILE + (Nav::CELLS_PER_TILE - 1));
        tile.MutableGateways().push_back(other);

        // A hand-authored crossing: two mouths that are not on any border, joined by a
        // link. This is the Booty Bay dock in miniature -- two places the ground does
        // not connect, and a line in a file that says a creature may jump between them.
        Nav::Gateway mouthA;
        mouthA.side = Nav::NavTile::SIDE_LINK;
        mouthA.region = 0;
        mouthA.firstZ = mouthA.lastZ = mouthA.z = -20.0f;
        mouthA.width = 3.0f;
        mouthA.x = 10.0f;
        mouthA.y = 20.0f;
        mouthA.cell = uint32_t(100 * Nav::CELLS_PER_TILE + 100);
        mouthA.layer = 0;
        tile.MutableGateways().push_back(mouthA);

        Nav::Gateway mouthB = mouthA;
        mouthB.cell = uint32_t(110 * Nav::CELLS_PER_TILE + 100);
        tile.MutableGateways().push_back(mouthB);

        Nav::Link link;
        link.fromGate = 2;
        link.toGate = 3;
        link.cost = 8.3f;
        link.bidirectional = true;
        tile.MutableLinks().push_back(link);

        tile.MutableGatewayCost().assign(4 * 4, 0.0f);
        tile.MutableGatewayCost()[0 * 4 + 1] = 137.5f;
        tile.MutableGatewayCost()[1 * 4 + 0] = 137.5f;
        return tile;
    }

    std::string TempPath()
    {
        // The suite runs on three toolchains and two operating systems, so the file
        // goes beside the test binary rather than anywhere that needs a temp-dir API.
        return "nav_tile_roundtrip.tmp";
    }
}

// ---------------------------------------------------------------------------

TEST(NavTile_StackedSurfacesAreFoundInHeightOrder)
{
    const Nav::NavTile tile = MakeTile();

    std::vector<Nav::Surface> surfaces;
    tile.SurfacesAt(2 * Nav::CELLS_PER_TILE + 7, surfaces);

    CHECK_EQ(surfaces.size(), size_t(2));
    CHECK(surfaces[0].z < surfaces[1].z);
    CHECK_EQ(surfaces[0].layer, uint16_t(0));
    CHECK_EQ(surfaces[1].layer, uint16_t(1));
    CHECK_EQ(surfaces[1].region, uint16_t(1));
}

TEST(NavTile_ACellWithoutTheStackedFlagIsNotSearched)
{
    const Nav::NavTile tile = MakeTile();

    std::vector<Nav::Surface> surfaces;
    tile.SurfacesAt(100 * Nav::CELLS_PER_TILE + 7, surfaces);

    CHECK_EQ(surfaces.size(), size_t(1));
}

TEST(NavTile_SurfaceUnderPrefersTheFloorBelowTheBody)
{
    const Nav::NavTile tile = MakeTile();
    const int cell = 2 * Nav::CELLS_PER_TILE + 7;

    // Standing on the balcony: the floor eight yards below is NOT what it is on.
    const Nav::Surface high = tile.SurfaceUnder(cell, -11.5f, 3.0f);
    CHECK(high.Valid());
    CHECK(std::fabs(high.z - (-12.0f)) < 0.2f);

    // Standing on the ground floor: the balcony is the ceiling, not the floor.
    const Nav::Surface low = tile.SurfaceUnder(cell, -19.8f, 3.0f);
    CHECK(low.Valid());
    CHECK(std::fabs(low.z - (-20.0f)) < 0.2f);
}

TEST(NavTile_SurfaceUnderRefusesWhenNothingIsInReach)
{
    const Nav::NavTile tile = MakeTile();
    const Nav::Surface none =
        tile.SurfaceUnder(100 * Nav::CELLS_PER_TILE + 7, 500.0f, 3.0f);
    CHECK(!none.Valid());
}

TEST(NavTile_ABlockedCellHasNoSurface)
{
    const Nav::NavTile tile = MakeTile();
    const int wall = 256 * Nav::CELLS_PER_TILE + 7;

    std::vector<Nav::Surface> surfaces;
    tile.SurfacesAt(wall, surfaces);
    CHECK(surfaces.empty());
    CHECK(!Nav::Walkable(tile.AreaAt(wall)));
}

// ---------------------------------------------------------------------------

TEST(NavTile_BorderHelpersAreEachOthersInverse)
{
    for (uint8_t side = 0; side < Nav::NavTile::SIDE_BORDER_COUNT; ++side)
    {
        for (int position = 0; position < Nav::CELLS_PER_TILE; position += 61)
        {
            const int cell = Nav::NavTile::BorderCell(side, position);
            CHECK_EQ(Nav::NavTile::BorderPosition(side, cell), position);
        }
    }
}

TEST(NavTile_FacingSideIsAnInvolution)
{
    // The neighbour of my neighbour across one border is me. Stitching walks this in
    // both directions and would join a tile to itself if it did not hold.
    for (uint8_t side = 0; side < Nav::NavTile::SIDE_BORDER_COUNT; ++side)
    {
        CHECK_EQ(Nav::NavTile::FacingSide(Nav::NavTile::FacingSide(side)), side);
    }
}

TEST(NavTile_SideTowardsRefusesDiagonals)
{
    // Tiles are joined across shared EDGES. A diagonal neighbour shares one corner,
    // which is not a crossing, and treating it as one is how a route slips between two
    // buildings through a gap of exactly zero yards.
    CHECK_EQ(Nav::NavTile::SideTowards(1, 1), uint8_t(Nav::NavTile::SIDE_NONE));
    CHECK_EQ(Nav::NavTile::SideTowards(0, 0), uint8_t(Nav::NavTile::SIDE_NONE));
    CHECK_EQ(Nav::NavTile::SideTowards(-1, 0), uint8_t(Nav::NavTile::SIDE_LOW_X));
}

// ---------------------------------------------------------------------------

TEST(NavTile_SurvivesItsOwnFile)
{
    const Nav::NavTile written = MakeTile();
    const std::string path = TempPath();

    CHECK(Nav::WriteNavTile(path, 571, written));

    Nav::NavTile read;
    CHECK(Nav::ReadNavTile(path, 571, read));

    CHECK_EQ(read.TileX(), written.TileX());
    CHECK_EQ(read.TileY(), written.TileY());
    CHECK(std::fabs(read.BaseZ() - written.BaseZ()) < 0.001f);
    CHECK(std::fabs(read.Params().maxClimb - 1.25f) < 0.001f);

    CHECK_EQ(read.Regions().size(), written.Regions().size());
    CHECK_EQ(read.Gateways().size(), written.Gateways().size());
    CHECK_EQ(read.Stacked().size(), written.Stacked().size());

    CHECK(std::fabs(read.GatewayCost(0, 1) - 137.5f) < 0.001f);

    // The link table, and the cell a gateway stands on -- both new in version 2, and
    // both indexed by the search without a bounds test, so a section that silently
    // vanished in the round trip would read as a tile with no links at all rather than
    // as a broken file.
    CHECK_EQ(read.Links().size(), size_t(1));
    CHECK_EQ(read.Gateways().size(), size_t(4));
    CHECK(read.Gateways()[2].IsLink());
    CHECK(!read.Gateways()[0].IsLink());
    CHECK_EQ(read.Gateways()[2].cell, written.Gateways()[2].cell);
    CHECK(std::fabs(read.Links()[0].cost - 8.3f) < 0.001f);
    CHECK(read.Links()[0].bidirectional);

    // And it is findable from either end, which is what the refinement asks.
    CHECK(read.LinkBetween(2, 3) != nullptr);
    CHECK(read.LinkBetween(3, 2) != nullptr);
    CHECK(read.LinkBetween(0, 1) == nullptr);

    // The area plane is run-length encoded and the payload planes hold walkable cells
    // only, so an off-by-one in either would show up as the wrong cell being blocked.
    for (int inTile = 0; inTile < Nav::CELLS_PER_TILE_SQ; inTile += 1013)
    {
        CHECK_EQ(read.AreaAt(inTile), written.AreaAt(inTile));
        if (Nav::Walkable(written.AreaAt(inTile)))
        {
            CHECK(std::fabs(read.HeightAt(inTile) - written.HeightAt(inTile)) < 0.01f);
            CHECK_EQ(read.RegionAt(inTile), written.RegionAt(inTile));
            CHECK_EQ(read.ClearanceAt(inTile), written.ClearanceAt(inTile));
        }
    }

    CHECK(!Nav::Walkable(read.AreaAt(256 * Nav::CELLS_PER_TILE + 7)));

    std::remove(path.c_str());
}

TEST(NavTile_RefusesATileBakedForAnotherMap)
{
    // The map id is in the header because the file NAME is not proof: a tile copied
    // between data sets parses perfectly and describes somewhere else entirely.
    const Nav::NavTile written = MakeTile();
    const std::string path = TempPath();

    CHECK(Nav::WriteNavTile(path, 571, written));

    Nav::NavTile read;
    CHECK(!Nav::ReadNavTile(path, 0, read));

    std::remove(path.c_str());
}

TEST(NavTile_RefusesATruncatedFile)
{
    // A refused tile is silence -- no ground, no route through it -- which is the loud
    // kind of failure. Reading a short file as a whole one is the quiet kind.
    const Nav::NavTile written = MakeTile();
    const std::string path = TempPath();

    CHECK(Nav::WriteNavTile(path, 571, written));

    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);

    std::string truncated = path + ".short";
    {
        std::FILE* in = std::fopen(path.c_str(), "rb");
        std::FILE* out = std::fopen(truncated.c_str(), "wb");
        CHECK(in != nullptr);
        CHECK(out != nullptr);

        std::vector<unsigned char> bytes(size_t(size / 2));
        const size_t got = std::fread(bytes.data(), 1, bytes.size(), in);
        std::fwrite(bytes.data(), 1, got, out);

        std::fclose(in);
        std::fclose(out);
    }

    Nav::NavTile read;
    CHECK(!Nav::ReadNavTile(truncated, 571, read));

    std::remove(path.c_str());
    std::remove(truncated.c_str());
}
