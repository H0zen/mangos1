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

// Where a navigation cell is.
//
// The one piece of this subsystem the baker and the server must agree on to the bit: a
// cell index computed while WRITING a tile and a cell index computed while READING one
// are the same arithmetic, or every payload in the file is read one step out of phase.
// It looks right in a debugger and puts creatures a hand's width inside walls.

#include "TestHarness.h"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"
#include "terrain/Terrain.hpp"

#include <cmath>

using world::terrain::TILE_SIZE;

// ---------------------------------------------------------------------------
// The grid is the terrain grid, refined. That is a claim with a consequence.

TEST(NavGrid_TileIndexAgreesWithTheTerrainGrid)
{
    // Not "both look plausible" -- identical, at every sample, including the negative
    // half of the map and the exact tile boundaries. Four nav cells span one terrain
    // height cell by construction, so `cell >> 9` has to be `grid >> 7`; if it ever is
    // not, the navigation and the collision are describing different tiles.
    for (int i = -400; i <= 400; ++i)
    {
        const float c = float(i) * TILE_SIZE * 0.083f;
        if (std::fabs(c) > 32.0f * TILE_SIZE)
        {
            continue;
        }

        CHECK_EQ(Nav::TileOfCell(Nav::CellIndex(c)), world::terrain::TileIndex(c));
    }
}

TEST(NavGrid_CellsPerHeightCellIsExact)
{
    CHECK_EQ(Nav::CELLS_PER_TILE, 512);
    CHECK_EQ(Nav::CELLS_PER_HEIGHT_CELL * world::terrain::GRID_PER_TILE,
             Nav::CELLS_PER_TILE);
}

// ---------------------------------------------------------------------------
// The two defects the terrain grid documents at length, inherited rather than
// re-derived -- and therefore worth proving are actually inherited.

TEST(NavGrid_FloorsRatherThanTruncatingBelowZero)
{
    // The strip just off the far positive corner has a NEGATIVE cell coordinate in
    // (-1, 0). Truncation toward zero sends it to cell 0 -- a point off one edge of the
    // map answered with the cell off the opposite one.
    const float justPast = 32.0f * TILE_SIZE + 0.5f;
    CHECK(Nav::CellIndex(justPast) < 0);
}

TEST(NavGrid_FarEdgeFoldsInwards)
{
    // The grid is half-open everywhere except its far edge, which is a real coordinate:
    // it lands exactly one past the last cell, so the extreme corner of the map would
    // answer "no cell" for the cell it is the corner of.
    const float farEdge = -32.0f * TILE_SIZE;
    CHECK_EQ(Nav::CellIndex(farEdge), Nav::CELLS_PER_MAP - 1);
    CHECK(Nav::OnMap(Nav::CellIndex(farEdge)));
}

// ---------------------------------------------------------------------------

TEST(NavGrid_CellCentreLandsBackInItsOwnCell)
{
    // A cell turned into a position and back is the same cell. This is what every
    // emitted route point relies on: the router names a cell, the point goes to its
    // centre, and anything that re-reads that point has to find the cell again.
    for (int cell = 0; cell < Nav::CELLS_PER_MAP; cell += 997)
    {
        CHECK_EQ(Nav::CellIndex(Nav::CellCentre(cell)), cell);
    }
}

TEST(NavGrid_CellIndicesGrowAsWorldCoordinatesFall)
{
    // Inherited from the terrain grid, and the single easiest thing to get backwards
    // when stitching tiles: the neighbour at cellX - 1 lives at LARGER world x.
    CHECK(Nav::CellCentre(1000) > Nav::CellCentre(1001));
}

TEST(NavGrid_SplitAndRejoinRoundTrips)
{
    for (int cell = 0; cell < Nav::CELLS_PER_MAP; cell += 613)
    {
        const int tile = Nav::TileOfCell(cell);
        const int local = Nav::LocalOfCell(cell);

        CHECK(local >= 0 && local < Nav::CELLS_PER_TILE);
        CHECK_EQ(Nav::GlobalCell(tile, local), cell);
    }
}

// ---------------------------------------------------------------------------
// Height quantisation: six centimetres of error, and it must saturate rather than wrap.

TEST(NavGrid_HeightRoundTripsWithinAQuantum)
{
    const float base = -137.5f;
    for (int i = 0; i < 200; ++i)
    {
        const float z = base + float(i) * 3.77f;
        const float back = Nav::RestoreZ(Nav::QuantiseZ(z, base), base);
        CHECK(std::fabs(back - z) <= Nav::Z_QUANTUM);
    }
}

TEST(NavGrid_HeightSaturatesInsteadOfWrapping)
{
    // A wrap would put a mountain top at the bottom of the sea and read as perfectly
    // valid data. Saturation is wrong by a bounded amount; wrapping is wrong by
    // everything.
    CHECK_EQ(Nav::QuantiseZ(-1000.0f, 0.0f), uint16_t(0));
    CHECK_EQ(Nav::QuantiseZ(999999.0f, 0.0f), uint16_t(65535));
}

TEST(NavGrid_ClearanceSaturatesAtOpenGround)
{
    CHECK_EQ(Nav::QuantiseClearance(-5.0f), uint8_t(0));
    CHECK_EQ(Nav::QuantiseClearance(1000.0f), uint8_t(255));
    CHECK(std::fabs(Nav::RestoreClearance(Nav::QuantiseClearance(2.0f)) - 2.0f) <=
          Nav::CLEARANCE_QUANTUM);
}

// ---------------------------------------------------------------------------

TEST(NavGrid_CellRefIndexesATileRowMajorInX)
{
    Nav::CellRef ref = Nav::CellAt(100.0f, -250.0f);
    CHECK(ref.Valid());
    CHECK_EQ(ref.InTile(), ref.LocalX() * Nav::CELLS_PER_TILE + ref.LocalY());
    CHECK(ref.InTile() >= 0 && ref.InTile() < Nav::CELLS_PER_TILE_SQ);
}

TEST(NavGrid_OffMapCoordinatesAreRejectedRatherThanClamped)
{
    // Clamping here would answer a query off the map with the edge cell, which is a
    // real position on real ground -- so the caller would never learn it had asked
    // about nowhere.
    const Nav::CellRef ref = Nav::CellAt(99999.0f, 0.0f);
    CHECK(!ref.Valid());
}
