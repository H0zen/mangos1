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

// A hand-authored crossing, end to end: the file, the store, and a route over it.
//
// The whole of it is one tile with a wall through the middle and no way round -- the
// Booty Bay dock in miniature. Two mouths face each other across the wall and a link
// joins them, and the assertion is the pair: WITH the link the router crosses, WITHOUT
// it the same ground is unroutable. Either half alone proves nothing. A route that
// succeeds might have found a way round the wall that was never meant to be there, and
// a route that fails might be failing for any of a dozen reasons; only the two together
// say that the link is what carried it.
//
// Nothing here needs a Map, a World or a database, which is the point of a router that
// takes a store and a profile rather than a Unit.

#include "TestHarness.h"

#include "nav/NavStore.hpp"
#include "nav/NavTile.hpp"
#include "nav/NavTileIO.hpp"
#include "nav/Router.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr int TILE_X = 32;
    constexpr int TILE_Y = 32;

    /// The walkable strip, in local cell coordinates. Small on purpose: the fine search
    /// walks every cell it is offered, and a whole tile of open ground would make this
    /// test a benchmark.
    constexpr int STRIP_LOW_X = 240;
    constexpr int STRIP_HIGH_X = 270;
    constexpr int STRIP_LOW_Y = 250;
    constexpr int STRIP_HIGH_Y = 260;

    /// The two columns nothing may stand on. Two and not one because a diagonal step
    /// slips through a single-column wall at its corner, which is a property of the
    /// eight-connected search and not of this test.
    constexpr int WALL_LOW_X = 255;
    constexpr int WALL_HIGH_X = 256;

    constexpr int MOUTH_A_X = 250;
    constexpr int MOUTH_B_X = 262;
    constexpr int MOUTH_Y = 255;

    constexpr float FLOOR_Z = -20.0f;

    int InTile(int localX, int localY)
    {
        return localX * Nav::CELLS_PER_TILE + localY;
    }

    float WorldX(int localX) { return Nav::CellCentre(Nav::GlobalCell(TILE_X, localX)); }
    float WorldY(int localY) { return Nav::CellCentre(Nav::GlobalCell(TILE_Y, localY)); }

    /// A mouth of a link: one cell, named outright, on no border at all.
    Nav::Gateway Mouth(int localX, uint16_t region)
    {
        Nav::Gateway g;
        g.side = Nav::NavTile::SIDE_LINK;
        g.first = 0;
        g.last = 0;
        g.region = region;
        g.firstZ = FLOOR_Z;
        g.lastZ = FLOOR_Z;
        g.width = 4.0f;
        g.x = WorldX(localX);
        g.y = WorldY(MOUTH_Y);
        g.z = FLOOR_Z;
        g.cell = uint32_t(InTile(localX, MOUTH_Y));
        g.layer = 0;
        return g;
    }

    /**
     * @brief One tile: a strip of floor cut in two by a wall, and two mouths facing.
     *
     * The cost matrix says UNREACHABLE between the mouths, which is the truth about the
     * ground and is what tells the refinement that the hop between them is a jump rather
     * than a walk it failed to find.
     */
    Nav::NavTile MakeSplitTile(bool withLink)
    {
        Nav::NavTile tile;
        tile.Reset(TILE_X, TILE_Y, FLOOR_Z - 10.0f);

        // Not called `near` and `far`: minwindef.h still defines both as empty macros,
        // and a local of either name vanishes on the MSVC leg of CI.
        Nav::Region westward;
        westward.areas = Nav::AreaBit(Nav::NavArea::Ground);
        westward.maxClearance = 8.0f;
        westward.minZ = FLOOR_Z;
        westward.maxZ = FLOOR_Z;

        tile.MutableRegions().push_back(westward);
        tile.MutableRegions().push_back(westward);

        for (int lx = STRIP_LOW_X; lx <= STRIP_HIGH_X; ++lx)
        {
            if (lx == WALL_LOW_X || lx == WALL_HIGH_X)
            {
                continue;
            }

            const uint16_t region = (lx < WALL_LOW_X) ? 0 : 1;

            for (int ly = STRIP_LOW_Y; ly <= STRIP_HIGH_Y; ++ly)
            {
                tile.SetCell(InTile(lx, ly), Nav::QuantiseZ(FLOOR_Z, tile.BaseZ()),
                             Nav::PackArea(Nav::NavArea::Ground, 0),
                             Nav::QuantiseClearance(8.0f), region);
                ++tile.MutableRegions()[region].cellCount;
            }
        }

        tile.MutableGateways().push_back(Mouth(MOUTH_A_X, 0));
        tile.MutableGateways().push_back(Mouth(MOUTH_B_X, 1));

        // No walk joins them, whatever the router would like to believe.
        tile.MutableGatewayCost() = {0.0f, Nav::NavTile::UNREACHABLE,
                                     Nav::NavTile::UNREACHABLE, 0.0f};

        if (withLink)
        {
            Nav::Link link;
            link.fromGate = 0;
            link.toGate = 1;
            link.cost = std::fabs(WorldX(MOUTH_A_X) - WorldX(MOUTH_B_X));
            link.bidirectional = true;
            tile.MutableLinks().push_back(link);
        }

        return tile;
    }

    /// A ground creature of no particular size.
    Nav::MoveProfile Walker()
    {
        Nav::MoveProfile profile;
        profile.canWalk = true;
        profile.maxClimb = 1.0f;
        profile.radius = 0.0f;
        return profile;
    }

    /**
     * @brief Write the tile, load it, and ask for the route across the wall.
     *
     * The map id differs between the two cases so that the two files, the two stores and
     * the two answers cannot borrow anything from each other.
     */
    Nav::Route RouteBetween(uint32_t mapId, bool withLink, int fromX, int toX,
                            const Nav::SearchBudget& budget)
    {
        Nav::SetNavDir(".");

        const std::string path = "./" + Nav::NavTileFileName(mapId, TILE_X, TILE_Y);
        const bool written = Nav::WriteNavTile(path, mapId, MakeSplitTile(withLink));

        Nav::Route route;
        if (!written)
        {
            return route;   // Unroutable by default; the CHECK below reports the write
        }

        Nav::NavStore store(mapId);
        const bool loaded = store.LoadTile(TILE_X, TILE_Y);

        if (loaded)
        {
            Nav::RouteRequest request;
            request.start = Geometry::Vector3(WorldX(fromX), WorldY(MOUTH_Y), FLOOR_Z);
            request.end = Geometry::Vector3(WorldX(toX), WorldY(MOUTH_Y), FLOOR_Z);
            request.profile = Walker();
            request.budget = budget;

            const Nav::Router router(store);
            router.Find(request, route);
        }

        std::remove(path.c_str());
        return route;
    }

    Nav::Route RouteAcross(uint32_t mapId, bool withLink)
    {
        return RouteBetween(mapId, withLink, STRIP_LOW_X + 5, STRIP_HIGH_X - 5,
                            Nav::SearchBudget());
    }

    float PlanLength(const Nav::Route& route)
    {
        float walked = 0.0f;
        for (size_t i = 1; i < route.points.size(); ++i)
        {
            const float dx = route.points[i].x - route.points[i - 1].x;
            const float dy = route.points[i].y - route.points[i - 1].y;
            walked += std::sqrt(dx * dx + dy * dy);
        }
        return walked;
    }

    /// The local x of the cell a route point stands over.
    int LocalXOf(const Geometry::Vector3& point)
    {
        return Nav::CellAt(point.x, point.y).LocalX();
    }
}

// ---------------------------------------------------------------------------

TEST(RouterLink_CarriesARouteAcrossGroundThatDoesNotConnect)
{
    const Nav::Route route = RouteAcross(701, true);

    CHECK(route.IsRouted());
    CHECK(route.points.size() >= size_t(2));

    // One segment steps from one side of the wall to the other. Not merely "the route
    // ends on the far side": the emitter drops every corner a straight walk makes
    // unnecessary, so if the wall had been walked round rather than jumped, the crossing
    // would show up as points threading past the ends of it instead of one segment
    // stepping over it.
    bool crossed = false;
    for (size_t i = 0; i + 1 < route.points.size(); ++i)
    {
        const int here = LocalXOf(route.points[i]);
        const int next = LocalXOf(route.points[i + 1]);
        if (here < WALL_LOW_X && next > WALL_HIGH_X)
        {
            crossed = true;
        }
    }
    CHECK(crossed);

    // And nothing was emitted standing inside the wall.
    for (const Geometry::Vector3& point : route.points)
    {
        const int lx = LocalXOf(point);
        CHECK(lx != WALL_LOW_X && lx != WALL_HIGH_X);
    }
}

// A flee is thirty yards by a rule of the game, and between the emitter changing and this
// being put back the mesh route ignored the cap entirely: a ten-yard bolt could come back
// two hundred yards long and be reported as a complete success.
//
// The cut lands ON the segment rather than at the corner before it, which is the half
// that is easy to get wrong. This route is open ground -- two points, one straight run --
// so a cut that stopped at the previous corner would stop at the start and the length
// would be zero. Asserting that it is CLOSE to the cap is what tells the two apart.
TEST(RouterLength_ARouteIsCutAtTheYardsTheCallerAllowed)
{
    constexpr float CAP = 4.0f;

    // Both ends west of the wall: one region, no jump involved, so what is measured is
    // the cap and nothing else.
    const Nav::Route route = RouteBetween(703, true, STRIP_LOW_X + 1, WALL_LOW_X - 1,
                                          Nav::SearchBudget::ForLength(CAP));

    REQUIRE(route.points.size() >= size_t(2));
    CHECK(route.UsedGeometry());
    CHECK(!route.IsRouted());
    CHECK(route.stop == Nav::RouteStop::LengthBudget);

    const float walked = PlanLength(route);
    CHECK(walked <= CAP + 0.01f);
    CHECK(walked > CAP - 0.5f);

    // And the destination was NOT welded back onto the end. A route that has been cut is
    // precisely one that does not reach the goal; moving its last point to the goal would
    // replace walked ground with a straight line through everything never looked at.
    const float dx = route.points.back().x - WorldX(WALL_LOW_X - 1);
    const float dy = route.points.back().y - WorldY(MOUTH_Y);
    CHECK(std::sqrt(dx * dx + dy * dy) > 1.0f);
}

// The same ground with no cap is one straight run to the goal. Without this the test
// above would pass on a router that simply failed to route at all.
TEST(RouterLength_WithoutACapTheSameGroundIsWalkedWhole)
{
    const Nav::Route route = RouteBetween(704, true, STRIP_LOW_X + 1, WALL_LOW_X - 1,
                                          Nav::SearchBudget());

    CHECK(route.IsRouted());
    CHECK(route.stop == Nav::RouteStop::Reached);
    CHECK(PlanLength(route) > 4.0f);
}

TEST(RouterLink_WithoutTheLinkTheSameGroundIsUnroutable)
{
    // The other half of the pair. Same cells, same wall, same mouths, same cost matrix
    // -- only the link table is empty, and the route must fail. If this ever passes, the
    // test above is proving something other than what it says.
    const Nav::Route route = RouteAcross(702, false);

    CHECK(route.Failed());
    CHECK(!route.UsedGeometry());
}
