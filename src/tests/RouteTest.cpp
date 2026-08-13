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

// The decisions the movement stack makes about a route, tested WITHOUT a world.
//
// None of this could be asserted on before. Every one of these answers lived either
// inside a bitmask that only the router wrote, or behind a call into a live Unit --
// and standing a Unit up needs a Map, which needs the database. The whole point of
// extracting Route and MoveProfile as values is that these cases now cost nothing to
// run, so the suite links no part of the game to run them.

#include "TestHarness.h"

#include "MoveMapSharedDefines.h"
#include "MoveProfile.h"
#include "Route.h"

#include <cstddef>

// ---------------------------------------------------------------------------
// Route: four states, and the three questions consumers ask of them.
// ---------------------------------------------------------------------------

static Route MakeRoute(RouteOutcome outcome, RouteStop stop)
{
    Route r;
    r.outcome = outcome;
    r.stop = stop;
    return r;
}

TEST(Route_DefaultRefuses)
{
    // A default-constructed route must not read as usable. The router assigns its
    // outcome on every branch, but "on every branch" is a property of today's code,
    // and the default is what protects the day one branch stops doing it.
    const Route r;
    CHECK(r.Failed());
    CHECK(!r.IsRouted());
    CHECK(!r.UsedGeometry());
    CHECK(!r.WillArrive());
    CHECK(r.points.empty());
}

TEST(Route_OutcomesAreExclusive)
{
    // The truth table the bitmask could not express. Each row is one outcome and the
    // three questions MotionFrame asks of it; the two middle rows are the whole reason
    // the questions are separate.
    struct Row
    {
        RouteOutcome outcome;
        bool         isRouted;
        bool         usedGeometry;
        bool         willArrive;
        bool         failed;
    };

    const Row rows[] =
    {
        { RouteOutcome::Routed,     true,  true,  true,  false },
        { RouteOutcome::Partial,    false, true,  false, false },
        { RouteOutcome::Direct,     false, false, true,  false },
        { RouteOutcome::Unroutable, false, false, false, true  },
    };

    for (const Row& row : rows)
    {
        const Route r = MakeRoute(row.outcome, RouteStop::Reached);
        CHECK_EQ(r.IsRouted(), row.isRouted);
        CHECK_EQ(r.UsedGeometry(), row.usedGeometry);
        CHECK_EQ(r.WillArrive(), row.willArrive);
        CHECK_EQ(r.Failed(), row.failed);
    }
}

TEST(Route_PartialAndDirectAreOpposites)
{
    // The distinction the old mask destroyed. PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH
    // -- a straight line taken because the mover can fly -- set the same NORMAL bit that
    // a genuinely routed path set, so "did this come off the navmesh" and "will this
    // mover arrive" had to be answered by two different maskings of one field, and
    // getting the masking wrong silently welded a straight line to a routed leg.
    const Route partial = MakeRoute(RouteOutcome::Partial, RouteStop::Wall);
    const Route direct = MakeRoute(RouteOutcome::Direct, RouteStop::NoMesh);

    // Real geometry that stops short: safe to weld, will not arrive.
    CHECK(partial.UsedGeometry());
    CHECK(!partial.WillArrive());

    // A line the mover is entitled to: it arrives, but nothing about it is routed.
    CHECK(!direct.UsedGeometry());
    CHECK(direct.WillArrive());

    // Neither is a failure, which is exactly why "did it fail" cannot separate them.
    CHECK(!partial.Failed());
    CHECK(!direct.Failed());
}

TEST(Route_StopIsIndependentOfOutcome)
{
    // A budget and a wall both produce a partial route and call for opposite responses:
    // re-planning from further along makes progress against a budget and never against
    // a wall. The outcome cannot carry that, which is why the stop is its own field.
    const Route wall = MakeRoute(RouteOutcome::Partial, RouteStop::Wall);
    const Route nodes = MakeRoute(RouteOutcome::Partial, RouteStop::NodeBudget);
    const Route polys = MakeRoute(RouteOutcome::Partial, RouteStop::PolyBudget);

    // Compared with CHECK rather than CHECK_EQ: the latter renders both sides with
    // std::to_string, which has no overload for a scoped enumeration.
    CHECK(wall.outcome == nodes.outcome);
    CHECK(wall.outcome == polys.outcome);
    CHECK(wall.stop != nodes.stop);
    CHECK(nodes.stop != polys.stop);
}

// ---------------------------------------------------------------------------
// MoveProfile: what the mover is permitted to do.
// ---------------------------------------------------------------------------

TEST(MoveProfile_DefaultPermitsNothing)
{
    const MoveProfile p;
    CHECK_EQ(p.includeFlags, uint16(0));
    CHECK(!p.MayGoDirect(true));
    CHECK(!p.MayGoDirect(false));
}

TEST(MoveProfile_MayGoDirectGatesOnLeavingTheMesh)
{
    // Players never leave the mesh however able they are: a client drives its own
    // movement and would be desynchronised by a server route through geometry it can
    // walk into. mayLeaveMesh is that rule, and it outranks both abilities.
    MoveProfile amphibiousPlayer;
    amphibiousPlayer.canSwim = true;
    amphibiousPlayer.canFly = true;
    amphibiousPlayer.mayLeaveMesh = false;

    CHECK(!amphibiousPlayer.MayGoDirect(true));
    CHECK(!amphibiousPlayer.MayGoDirect(false));
}

TEST(MoveProfile_MayGoDirectPicksTheAbilityTheGroundCallsFor)
{
    // Which ability answers depends on WHERE the off-mesh ground is, not on which
    // abilities the mover happens to have. A swimmer that cannot fly is refused dry
    // ground, and a flier that cannot swim is refused water -- the pair of cases the
    // old nested if/else got right and no test ever held it to.
    MoveProfile swimmer;
    swimmer.mayLeaveMesh = true;
    swimmer.canSwim = true;
    swimmer.canFly = false;

    CHECK(swimmer.MayGoDirect(true));    // under water: swims
    CHECK(!swimmer.MayGoDirect(false));  // dry: cannot

    MoveProfile flier;
    flier.mayLeaveMesh = true;
    flier.canSwim = false;
    flier.canFly = true;

    CHECK(!flier.MayGoDirect(true));     // under water: cannot
    CHECK(flier.MayGoDirect(false));     // dry: flies

    MoveProfile grounded;
    grounded.mayLeaveMesh = true;

    CHECK(!grounded.MayGoDirect(true));
    CHECK(!grounded.MayGoDirect(false));
}

// ---------------------------------------------------------------------------
// The baked tile header: what a stale bake has to be caught by.
// ---------------------------------------------------------------------------

TEST(MmapTileHeader_DescribesWhatItWasBuiltAgainst)
{
    const MmapTileHeader header;

    CHECK_EQ(header.mmapMagic, uint32(MMAP_MAGIC));
    CHECK_EQ(header.mmapVersion, uint32(MMAP_VERSION));
    CHECK_EQ(header.dtVersion, uint32(DT_NAVMESH_VERSION));

    // The field that catches the failure DT_NAVMESH_VERSION cannot. dtLink embeds a
    // dtPolyRef and dtCreateNavMeshData sizes the tile blob from sizeof(dtLink), so
    // flipping DT_POLYREF64 moves every offset past this header while leaving both
    // version numbers alone.
    CHECK_EQ(header.polyRefSize, uint32(sizeof(dtPolyRef)));
    CHECK(header.polyRefSize == 4u || header.polyRefSize == 8u);

    CHECK_EQ(header.flags, uint32(MMAP_TILE_USES_LIQUIDS));
}

TEST(MmapTileHeader_LayoutIsSixWords)
{
    // Written with fwrite and read back by a possibly different compiler, so its size
    // is part of the file format. Six uint32 admit no padding on any implementation;
    // the `bool : 1` bitfield this replaced did not have that guarantee.
    CHECK_EQ(sizeof(MmapTileHeader), size_t(24));
}

// ---------------------------------------------------------------------------
// Area to flags: the version 7 defect, held down.
// ---------------------------------------------------------------------------

TEST(NavAreaToFlags_KeepsSurfacesTellingThemselvesApart)
{
    // Detour tests the filter against polyFlags and never against the area id. Writing
    // a bare 1 for every walkable polygon told every query that the whole world was
    // ground: a swimmer masking WATER|MAGMA|SLIME matched nothing and could not cross
    // its own lake, while a walker masking GROUND was cleared to walk over magma.
    const uint16 swimmer = NAV_WATER | NAV_MAGMA | NAV_SLIME;
    const uint16 walker = NAV_GROUND;

    CHECK((NavAreaToFlags(NAV_WATER) & swimmer) != 0);
    CHECK((NavAreaToFlags(NAV_MAGMA) & swimmer) != 0);
    CHECK((NavAreaToFlags(NAV_SLIME) & swimmer) != 0);
    CHECK((NavAreaToFlags(NAV_GROUND) & swimmer) == 0);

    CHECK((NavAreaToFlags(NAV_GROUND) & walker) != 0);
    CHECK((NavAreaToFlags(NAV_MAGMA) & walker) == 0);
    CHECK((NavAreaToFlags(NAV_WATER) & walker) == 0);

    // And an area id is a valid index into Detour's per-area cost table, which is what
    // lets the surfaces be PRICED rather than merely permitted.
    CHECK(NAV_SLIME < DT_MAX_AREAS);
    CHECK(NAV_WATER < DT_MAX_AREAS);
}
