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

// A HEIGHTMAP THROUGH THE WHOLE BAKER. `NavTileTest` builds tiles by hand and
// `NavGridTest` checks the arithmetic, so the one path nothing covered was the one that
// matters most: ground goes in, a mesh comes out. It went uncovered long enough for a
// hillside to be unbakeable without a single test turning red.
//
// The ramps here are exact planes at a known angle, which makes the assertions
// statements about the constants rather than about a landscape. A bank at 50 degrees is
// inside `maxSlopeDeg = 55` and must bake. One at 70 is outside it and must not. Either
// on its own proves little -- a baker that accepts everything passes the first, a baker
// that accepts nothing passes the second -- so the pair is the test.
//
// The 50-degree case is what a one-yard `maxClimb` used to refuse: 1.0417 yards of cell
// times tan(50) is 1.24 yards of rise, over the step, so no cell linked to its
// neighbour, every cell became a region of one, and MIN_REGION_NODES swept the hill into
// nothing. See Nav::ClimbWindow.

#include "TestHarness.h"

#include "nav/NavBuilder.hpp"
#include "nav/NavGrid.hpp"
#include "nav/NavTile.hpp"
#include "terrain/FusedTerrain.hpp"
#include "terrain/Terrain.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
    using world::terrain::GRID_PER_TILE;
    using world::terrain::ITileSource;
    using world::terrain::TerrainTile;
    using world::terrain::V9_SIDE;

    constexpr uint32_t MAP_ID = 0;
    constexpr int TILE_X = 32;
    constexpr int TILE_Y = 32;

    /// One terrain height cell, in yards. The ramp's rise is quoted per one of these.
    constexpr float HEIGHT_CELL = world::terrain::TILE_SIZE / float(GRID_PER_TILE);

    /**
     * @brief One tile whose ground is an exact plane at `degrees`, and nothing else.
     *
     * V8 is the centre of each height cell and is filled from the plane rather than
     * left at zero: `TerrainHeight` interpolates four triangles that all meet at that
     * centre, so a V8 that disagrees with its four corners is a dimple in every cell and
     * the surface stops being the plane the test is about.
     *
     * Only the one tile exists. A neighbour would make the margin sample a second ramp
     * with a seam between them, which is a different question than this one.
     */
    class RampSource : public ITileSource
    {
    public:
        explicit RampSource(float degrees)
        {
            m_tile = std::make_shared<TerrainTile>();
            m_tile->tx = TILE_X;
            m_tile->ty = TILE_Y;
            m_tile->hasTerrain = true;

            const float rise = HEIGHT_CELL * std::tan(degrees * 3.14159265f / 180.0f);

            m_tile->v9.resize(size_t(V9_SIDE) * V9_SIDE);
            for (int a = 0; a < V9_SIDE; ++a)
            {
                for (int b = 0; b < V9_SIDE; ++b)
                {
                    m_tile->v9[size_t(a) * V9_SIDE + b] = float(a) * rise;
                }
            }

            m_tile->v8.resize(size_t(GRID_PER_TILE) * GRID_PER_TILE);
            for (int a = 0; a < GRID_PER_TILE; ++a)
            {
                for (int b = 0; b < GRID_PER_TILE; ++b)
                {
                    m_tile->v8[size_t(a) * GRID_PER_TILE + b] = (float(a) + 0.5f) * rise;
                }
            }
        }

        std::shared_ptr<TerrainTile> Load(uint32_t mapId, int tx, int ty) override
        {
            const bool mine = mapId == MAP_ID && tx == TILE_X && ty == TILE_Y;
            return mine ? m_tile : nullptr;
        }

    private:
        std::shared_ptr<TerrainTile> m_tile;
    };

    /// How many of the tile's own cells carry a walkable surface, out of 512x512.
    size_t WalkableCells(const Nav::NavTile& tile)
    {
        size_t count = 0;
        std::vector<Nav::Surface> surfaces;

        for (int lx = 0; lx < Nav::CELLS_PER_TILE; ++lx)
        {
            for (int ly = 0; ly < Nav::CELLS_PER_TILE; ++ly)
            {
                tile.SurfacesAt(lx * Nav::CELLS_PER_TILE + ly, surfaces);
                if (!surfaces.empty())
                {
                    ++count;
                }
            }
        }

        return count;
    }

    bool BakeRamp(float degrees, Nav::NavTile& out)
    {
        world::terrain::FusedTerrain terrain(MAP_ID, std::make_shared<RampSource>(degrees));
        return Nav::BuildNavTile(terrain, TILE_X, TILE_Y, Nav::BuildParams(), out);
    }
}

// The control. Nothing about flat ground was ever in doubt; it is here so that a
// failure in the ramp cases can be read as being about the slope and not about the
// harness, the source, or the tile the source hands back.
TEST(NavBuilder_FlatGroundBakes)
{
    Nav::NavTile tile;
    REQUIRE(BakeRamp(0.0f, tile));

    const size_t walkable = WalkableCells(tile);
    CHECK(walkable > size_t(Nav::CELLS_PER_TILE) * Nav::CELLS_PER_TILE * 9 / 10);
    CHECK(!tile.Regions().empty());
}

// The regression. A 50-degree bank is walkable by the constants the bake ships, and
// before Nav::ClimbWindow it produced no mesh at all -- not a thin one, none: every
// cell isolated, every region one node, all of them under MIN_REGION_NODES.
TEST(NavBuilder_AWalkableHillsideIsNotSweptAway)
{
    Nav::NavTile tile;
    REQUIRE(BakeRamp(50.0f, tile));

    const size_t walkable = WalkableCells(tile);
    CHECK(walkable > size_t(Nav::CELLS_PER_TILE) * Nav::CELLS_PER_TILE * 9 / 10);
    CHECK(!tile.Regions().empty());
}

// And the other half of the pair, without which the first proves only that the baker
// says yes. Ground past `maxSlopeDeg` is blocked by the slope pass, so nothing is left
// to flood and the tile is refused whole -- which for a map that is nothing but cliff
// is the right answer.
//
// This case is also what showed that an axis with ground on only one side must be
// measured from that side rather than skipped: skipping left the outermost row of the
// sampled patch walkable at any angle, and a 70-degree cliff baked 1024 cells in two
// regions -- the two rows whose outward neighbour was off the sampled tile.
TEST(NavBuilder_GroundPastTheSlopeLimitIsRefused)
{
    Nav::NavTile tile;
    CHECK(!BakeRamp(70.0f, tile));
}

// The window is the larger of the two limits, never the climb alone. Stated directly so
// the arithmetic behind the two cases above is checkable without baking anything.
TEST(NavBuilder_ClimbWindowFollowsWhicheverLimitIsLooser)
{
    // 55 degrees over one cell earns more rise than a one-yard step, so the slope wins.
    const float wide = Nav::ClimbWindow(1.0f, 55.0f, Nav::CELL_SIZE);
    CHECK(wide > 1.48f && wide < 1.49f);

    // A 50-degree bank rises less than that, so it links.
    CHECK(Nav::CELL_SIZE * std::tan(50.0f * 3.14159265f / 180.0f) < wide);

    // A 70-degree one rises more, so it does not.
    CHECK(Nav::CELL_SIZE * std::tan(70.0f * 3.14159265f / 180.0f) > wide);

    // And a slope limit too tight to earn a step back leaves the step in charge.
    CHECK_EQ(Nav::ClimbWindow(1.0f, 5.0f, Nav::CELL_SIZE), 1.0f);
}
