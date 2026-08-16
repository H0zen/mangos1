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
// extracting Nav::Route and Nav::MoveProfile as values is that these cases now cost nothing to
// run, so the suite links no part of the game to run them.

#include "TestHarness.h"

#include "nav/NavArea.hpp"
#include "nav/Route.hpp"
#include "nav/SearchBudget.hpp"

#include <cstddef>
#include <initializer_list>
#include <vector>

// ---------------------------------------------------------------------------
// Nav::Route: four states, and the three questions consumers ask of them.
// ---------------------------------------------------------------------------

static Nav::Route MakeRoute(Nav::RouteOutcome outcome, Nav::RouteStop stop)
{
    Nav::Route r;
    r.outcome = outcome;
    r.stop = stop;
    return r;
}

TEST(Route_DefaultRefuses)
{
    // A default-constructed route must not read as usable. The router assigns its
    // outcome on every branch, but "on every branch" is a property of today's code,
    // and the default is what protects the day one branch stops doing it.
    const Nav::Route r;
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
        Nav::RouteOutcome outcome;
        bool         isRouted;
        bool         usedGeometry;
        bool         willArrive;
        bool         failed;
    };

    const Row rows[] =
    {
        { Nav::RouteOutcome::Routed,     true,  true,  true,  false },
        { Nav::RouteOutcome::Partial,    false, true,  false, false },
        { Nav::RouteOutcome::Direct,     false, false, true,  false },
        { Nav::RouteOutcome::Unroutable, false, false, false, true  },
    };

    for (const Row& row : rows)
    {
        const Nav::Route r = MakeRoute(row.outcome, Nav::RouteStop::Reached);
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
    const Nav::Route partial = MakeRoute(Nav::RouteOutcome::Partial, Nav::RouteStop::Wall);
    const Nav::Route direct = MakeRoute(Nav::RouteOutcome::Direct, Nav::RouteStop::NoMesh);

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
    const Nav::Route wall = MakeRoute(Nav::RouteOutcome::Partial, Nav::RouteStop::Wall);
    const Nav::Route nodes = MakeRoute(Nav::RouteOutcome::Partial, Nav::RouteStop::NodeBudget);
    const Nav::Route points = MakeRoute(Nav::RouteOutcome::Partial, Nav::RouteStop::PointBudget);

    // Compared with CHECK rather than CHECK_EQ: the latter renders both sides with
    // std::to_string, which has no overload for a scoped enumeration.
    CHECK(wall.outcome == nodes.outcome);
    CHECK(wall.outcome == points.outcome);
    CHECK(wall.stop != nodes.stop);
    CHECK(nodes.stop != points.stop);
}

// ---------------------------------------------------------------------------
// Nav::MoveProfile: what the mover is permitted to do.
// ---------------------------------------------------------------------------

TEST(MoveProfile_WalkerStaysOffTheWaterSkin)
{
    // The water skin is stacked above the seabed. A walking amphibian must keep
    // the floor; admitting it to both layers is how a makrura hopped.
    Nav::MoveProfile crab;
    crab.canWalk = true;
    crab.canSwim = true;
    crab.allowedAreas = uint16_t(Nav::AREAS_WALKABLE | Nav::AreaBit(Nav::NavArea::Water));

    CHECK(crab.Admits(Nav::NavArea::Water));
    CHECK(!crab.AdmitsGround(uint8_t(Nav::NavArea::Water)));
    CHECK(crab.AdmitsGround(uint8_t(Nav::NavArea::Ground)));
    CHECK(crab.AdmitsGround(uint8_t(Nav::NavArea::Shallow)));

    Nav::MoveProfile fish;
    fish.canWalk = false;
    fish.canSwim = true;
    fish.allowedAreas = Nav::AREAS_LIQUID;

    CHECK(fish.AdmitsGround(uint8_t(Nav::NavArea::Water)));
}

// The other half of the same rule, and the one that was missing. A pet that is
// ACTUALLY SWIMMING has no seabed within reach to stand on, so the skin is the only
// ground it has; refusing it there made the route OffMesh, movement fell through to
// a straight line, and the pet swam off across the bay.
//
// The crab above and this are the same creature in two situations, which is why the
// question is asked of `inWater` and not of `canWalk`.
TEST(MoveProfile_ASwimmingWalkerRidesTheSkin)
{
    Nav::MoveProfile pet;
    pet.canWalk = true;
    pet.canSwim = true;
    pet.allowedAreas = uint16_t(Nav::AREAS_WALKABLE | Nav::AreaBit(Nav::NavArea::Water));

    // On the shore: the seabed is what it uses, exactly as before.
    CHECK(!pet.AdmitsGround(uint8_t(Nav::NavArea::Water)));

    pet.inWater = true;

    CHECK(pet.AdmitsGround(uint8_t(Nav::NavArea::Water)));
    // And it has not stopped being able to walk out again.
    CHECK(pet.AdmitsGround(uint8_t(Nav::NavArea::Ground)));
    CHECK(pet.AdmitsGround(uint8_t(Nav::NavArea::Shallow)));
}

TEST(MoveProfile_DefaultPermitsNothing)
{
    const Nav::MoveProfile p;
    CHECK_EQ(p.allowedAreas, uint16_t(Nav::AREAS_WALKABLE));
    CHECK(!p.MayGoDirect(true));
    CHECK(!p.MayGoDirect(false));
}

TEST(MoveProfile_MayGoDirectGatesOnLeavingTheMesh)
{
    // Players never leave the mesh however able they are: a client drives its own
    // movement and would be desynchronised by a server route through geometry it can
    // walk into. mayLeaveMesh is that rule, and it outranks both abilities.
    Nav::MoveProfile amphibiousPlayer;
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
    Nav::MoveProfile swimmer;
    swimmer.mayLeaveMesh = true;
    swimmer.canSwim = true;
    swimmer.canFly = false;

    CHECK(swimmer.MayGoDirect(true));    // under water: swims
    CHECK(!swimmer.MayGoDirect(false));  // dry: cannot

    Nav::MoveProfile flier;
    flier.mayLeaveMesh = true;
    flier.canSwim = false;
    flier.canFly = true;

    CHECK(!flier.MayGoDirect(true));     // under water: cannot
    CHECK(flier.MayGoDirect(false));     // dry: flies

    Nav::MoveProfile grounded;
    grounded.mayLeaveMesh = true;

    CHECK(!grounded.MayGoDirect(true));
    CHECK(!grounded.MayGoDirect(false));
}

TEST(SearchBudget_WithinRefusesInsteadOfClipping)
{
    const Nav::SearchBudget clip = Nav::SearchBudget::ForLength(30.0f);
    CHECK_EQ(clip.maxLength, 30.0f);
    CHECK(!clip.rejectIfLonger);

    const Nav::SearchBudget reject = Nav::SearchBudget::Within(30.0f);
    CHECK_EQ(reject.maxLength, 30.0f);
    CHECK(reject.rejectIfLonger);
}
