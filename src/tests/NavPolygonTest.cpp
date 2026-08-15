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

// THE POLYGON LAYER DESCRIBES THE SAME GROUND THE CELLS DID, OR IT IS WORTHLESS.
//
// A query algorithm defined over convex regions is only as honest as the claim that the
// regions ARE the walkable set. So that claim is what is asserted here, three ways that
// fail differently:
//
//   - every walkable cell is covered, and covered once -- the areas sum to the count;
//   - no rectangle contains a cell that was not walkable, checked cell by cell;
//   - two regions are never merged into one rectangle.
//
// A cover that is merely a superset would let a route cross ground nothing stands on,
// which is the one failure a navigation mesh must not have; a partition that misses
// cells would lose standing places silently, which is how six tiles of Azeroth went
// dark earlier on this branch.

#include "TestHarness.h"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"
#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace
{
    constexpr int SIDE = Nav::CELLS_PER_TILE;

    /// A tile whose walkable set is whatever `shape` says, at a flat height.
    ///
    /// Built through the same Reset/plane path the baker uses rather than by writing
    /// members, so a change to how a tile stores its cells breaks this test rather than
    /// letting it keep passing against a representation nothing else uses any more.
    template <typename Shape>
    void Paint(Nav::NavTile& tile, const Shape& shape)
    {
        tile.Reset(32, 32, -1.0f);

        Nav::Region region;
        region.areas = Nav::AreaBit(Nav::NavArea::Ground);
        region.maxClearance = 4.0f;
        region.minZ = 0.0f;
        region.maxZ = 0.0f;
        tile.MutableRegions().assign(size_t(shape.Regions()), region);

        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                const int owner = shape(x, y);
                if (owner < 0)
                {
                    continue;
                }

                tile.SetCell(x * SIDE + y, Nav::QuantiseZ(0.0f, tile.BaseZ()),
                             Nav::PackArea(Nav::NavArea::Ground, 0),
                             Nav::QuantiseClearance(4.0f), uint16_t(owner));
            }
        }
    }

    /// Every walkable cell covered exactly once, and nothing else covered at all.
    void CheckPartition(const Nav::NavTile& tile,
                        const std::vector<Nav::NavRect>& rects)
    {
        std::vector<uint8_t> hits(size_t(SIDE) * SIDE, 0);

        for (const Nav::NavRect& r : rects)
        {
            REQUIRE(r.x0 <= r.x1 && r.y0 <= r.y1);
            for (int x = r.x0; x <= r.x1; ++x)
            {
                for (int y = r.y0; y <= r.y1; ++y)
                {
                    ++hits[size_t(x) * SIDE + size_t(y)];
                }
            }
        }

        std::vector<Nav::Surface> surfaces;
        size_t walkable = 0;
        size_t uncovered = 0;
        size_t spurious = 0;
        size_t doubled = 0;

        for (int cell = 0; cell < SIDE * SIDE; ++cell)
        {
            tile.SurfacesAt(cell, surfaces);
            bool ok = false;
            for (const Nav::Surface& s : surfaces)
            {
                ok = ok || s.Valid();
            }

            if (ok)
            {
                ++walkable;
                if (hits[size_t(cell)] == 0)
                {
                    ++uncovered;
                }
                else if (hits[size_t(cell)] > 1)
                {
                    ++doubled;
                }
            }
            else if (hits[size_t(cell)] != 0)
            {
                ++spurious;
            }
        }

        CHECK_EQ(uncovered, size_t(0));
        CHECK_EQ(spurious, size_t(0));
        CHECK_EQ(doubled, size_t(0));
        CHECK_EQ(walkable, size_t(Nav::WalkableCellCount(tile)));
    }

    struct WholeTile
    {
        int operator()(int, int) const { return 0; }
        int Regions() const { return 1; }
    };

    /// Two regions meeting along a straight seam. A rectangle that spanned the seam
    /// would join two things the flood decided were separate.
    struct TwoRooms
    {
        int operator()(int x, int) const { return x < SIDE / 2 ? 0 : 1; }
        int Regions() const { return 2; }
    };

    /// A ring of ground round a hole, so the scan has to stop and restart on every row.
    struct Ring
    {
        int operator()(int x, int y) const
        {
            const bool hole = x > 100 && x < 400 && y > 100 && y < 400;
            return hole ? -1 : 0;
        }
        int Regions() const { return 1; }
    };
}

// The easy one, and the one that says whether the idea pays. Open ground is one
// rectangle -- 262144 cells described by four numbers.
TEST(NavPolygon_OpenGroundCollapsesToOneRectangle)
{
    Nav::NavTile tile;
    Paint(tile, WholeTile());

    const std::vector<Nav::NavRect> rects = Nav::DecomposeTile(tile);
    CHECK_EQ(rects.size(), size_t(1));
    if (!rects.empty())
    {
        CHECK_EQ(rects.front().Cells(), uint32_t(SIDE) * uint32_t(SIDE));
    }
    CheckPartition(tile, rects);
}

// A rectangle may never span two regions: the coarse structure of the mesh is the
// region graph, and merging across it would route through a wall the flood found.
TEST(NavPolygon_ARectangleNeverSpansTwoRegions)
{
    Nav::NavTile tile;
    Paint(tile, TwoRooms());

    const std::vector<Nav::NavRect> rects = Nav::DecomposeTile(tile);
    CHECK_EQ(rects.size(), size_t(2));
    CheckPartition(tile, rects);

    for (const Nav::NavRect& r : rects)
    {
        const bool leftOfSeam = r.x1 < SIDE / 2;
        const bool rightOfSeam = r.x0 >= SIDE / 2;
        CHECK(leftOfSeam || rightOfSeam);
    }
}

// The shape that decides whether this is a partition or a cover: ground round a hole,
// where a lazy scan either swallows the hole or emits overlapping strips.
TEST(NavPolygon_AHoleIsNotSwallowed)
{
    Nav::NavTile tile;
    Paint(tile, Ring());

    const std::vector<Nav::NavRect> rects = Nav::DecomposeTile(tile);
    CheckPartition(tile, rects);

    // Worth stating as a number as well as a property: a 512-square with a 299-square
    // hole is a handful of rectangles, not a quarter of a million cells.
    CHECK(rects.size() < 16);
}
