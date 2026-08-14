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

// A PASS IS A MEASUREMENT, NOT A THRESHOLD.
//
// The shapes here have their critical points written into them, so what is asserted is
// the count and the height of things the sweep must find: two bowls with one pass between
// them, and the pass at the height of the ridge's lowest notch -- to the cell.
//
// The last case is the one that says why persistence is worth carrying. A dimple half a
// yard deep produces a saddle exactly as a canyon does; the only thing that distinguishes
// them is how far the ground falls away either side of it, and that number is in yards
// rather than in cells anyone counted.

#include "TestHarness.h"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"
#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"
#include "nav/ReebGraph.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdint>

namespace
{
    constexpr int SIDE = Nav::CELLS_PER_TILE;

    /// `height(x, y, z)` returns false for a hole, or true with the floor height.
    template <typename Shape>
    void Paint(Nav::NavTile& tile, const Shape& shape)
    {
        tile.Reset(32, 32, -200.0f);
        tile.SetParams(Nav::TileParams());

        Nav::Region region;
        region.areas = Nav::AreaBit(Nav::NavArea::Ground);
        region.maxClearance = 8.0f;
        region.minZ = -100.0f;
        region.maxZ = 100.0f;
        tile.MutableRegions().assign(1, region);

        for (int x = 0; x < SIDE; ++x)
        {
            for (int y = 0; y < SIDE; ++y)
            {
                float z = 0.0f;
                if (!shape(x, y, z))
                {
                    continue;
                }

                tile.SetCell(x * SIDE + y, Nav::QuantiseZ(z, tile.BaseZ()),
                             Nav::PackArea(Nav::NavArea::Ground, 0),
                             Nav::QuantiseClearance(8.0f), 0);
            }
        }
    }

    /// Dead flat. One basin, no pass, and NO summit: level ground rises to nothing.
    struct Flat
    {
        bool operator()(int, int, float& z) const
        {
            z = 0.0f;
            return true;
        }
    };

    /**
     * @brief Two bowls with a ridge between them, notched at one place.
     *
     * The ridge runs along x = 256 at height 20. One notch, ten cells long, drops it to
     * height 5. Either side the ground falls to 0. So: two minima at 0, one saddle at
     * 5, persistence 5.
     */
    struct TwoBowls
    {
        static constexpr int RIDGE = 256;
        static constexpr float FLOOR = 0.0f;
        static constexpr float CREST = 20.0f;
        static constexpr float NOTCH = 5.0f;
        static constexpr int NOTCH_LOW = 250;
        static constexpr int NOTCH_HIGH = 259;

        bool operator()(int x, int y, float& z) const
        {
            if (x == RIDGE)
            {
                z = (y >= NOTCH_LOW && y <= NOTCH_HIGH) ? NOTCH : CREST;
                return true;
            }

            z = FLOOR;
            return true;
        }
    };

    /// The same, but the notch is only half a yard above the floor: a puddle rim, not a
    /// pass. Persistence 0.5.
    struct ShallowDimple
    {
        bool operator()(int x, int y, float& z) const
        {
            if (x == TwoBowls::RIDGE)
            {
                z = (y >= TwoBowls::NOTCH_LOW && y <= TwoBowls::NOTCH_HIGH) ? 0.5f
                                                                            : 20.0f;
                return true;
            }

            z = 0.0f;
            return true;
        }
    };
}

// Flat ground has one basin and nothing else to say about itself.
TEST(Reeb_FlatGroundIsOneBasin)
{
    Nav::NavTile tile;
    Paint(tile, Flat());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::ReebGraph graph = Nav::BuildReebGraph(tile, plan, 1.0f);

    CHECK_EQ(graph.basins.size(), size_t(1));
    CHECK_EQ(graph.passes.size(), size_t(0));
    CHECK_EQ(graph.arcs.size(), size_t(0));

    // AND no summits. Flat ground rises to nothing, so it has no peak -- which sounds
    // obvious and was not what the first version did: "no neighbour strictly higher" is
    // true of every cell of a plain, and map 0 produced tens of millions of them.
    CHECK_EQ(graph.summits.size(), size_t(0));
}

// A single hill has one peak, not a plateau's worth. The shape is a cone, so exactly
// one cell is above all its neighbours and every other cell has one above it.
TEST(Reeb_AHillHasOneSummit)
{
    struct Cone
    {
        bool operator()(int x, int y, float& z) const
        {
            const float dx = static_cast<float>(x - 256);
            const float dy = static_cast<float>(y - 256);
            z = 40.0f - std::sqrt(dx * dx + dy * dy) * 0.1f;
            return true;
        }
    };

    Nav::NavTile tile;
    Paint(tile, Cone());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::ReebGraph graph = Nav::BuildReebGraph(tile, plan, 1.0f);

    CHECK_EQ(graph.summits.size(), size_t(1));
    if (!graph.summits.empty())
    {
        const int x = int(graph.summits.front().cell) / SIDE;
        const int y = int(graph.summits.front().cell) % SIDE;
        CHECK(std::abs(x - 256) <= 1);
        CHECK(std::abs(y - 256) <= 1);
    }
}

// Two bowls, one notch: exactly one pass, at the notch, five yards above the floors.
// This is the structure the coarse search wants and the thing region-and-gateway can
// only express where the bake happened to cut a tile.
TEST(Reeb_ARidgeWithANotchIsOnePass)
{
    Nav::NavTile tile;
    Paint(tile, TwoBowls());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);
    const Nav::ReebGraph graph = Nav::BuildReebGraph(tile, plan, 1.0f);

    CHECK_EQ(graph.basins.size(), size_t(2));
    REQUIRE(graph.passes.size() == size_t(1));

    const Nav::CriticalPoint& pass = graph.passes.front();
    CHECK(std::fabs(pass.z - TwoBowls::NOTCH) < 0.1f);
    CHECK(std::fabs(pass.persistence - (TwoBowls::NOTCH - TwoBowls::FLOOR)) < 0.1f);

    // It joins the two basins, and it sits in the notch.
    CHECK(pass.basinA != pass.basinB);
    const int y = int(pass.cell) % SIDE;
    CHECK(y >= TwoBowls::NOTCH_LOW && y <= TwoBowls::NOTCH_HIGH);
    CHECK_EQ(int(pass.cell) / SIDE, TwoBowls::RIDGE);

    CHECK_EQ(graph.arcs.size(), size_t(1));
}

// Half a yard is not a pass. The basins still merge -- the ground really is joined --
// but nothing is recorded, because persistence says the feature was never there.
TEST(Reeb_APuddleRimIsNotAPass)
{
    Nav::NavTile tile;
    Paint(tile, ShallowDimple());

    const Nav::TilePlan plan = Nav::ReadTilePlan(tile);

    const Nav::ReebGraph strict = Nav::BuildReebGraph(tile, plan, 1.0f);
    CHECK_EQ(strict.passes.size(), size_t(0));

    // Lower the bar under the feature's own depth and the same sweep reports it, which
    // is what makes this a measurement rather than a filter that hides things.
    const Nav::ReebGraph loose = Nav::BuildReebGraph(tile, plan, 0.25f);
    CHECK_EQ(loose.passes.size(), size_t(1));
    if (!loose.passes.empty())
    {
        CHECK(std::fabs(loose.passes.front().persistence - 0.5f) < 0.1f);
    }
}
