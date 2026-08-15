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
 */

/**
 * @file RosterTest.cpp
 * @brief The rules that decide who drives a unit, asserted without a unit.
 *
 * These rules have governed every creature in the world since the beginning and have
 * never had a single test, because they were only observable by standing up a map, a
 * creature and an AI. They were spread across four near-identical functions, two flag
 * bits and a side list, and the reason they were spread was mechanical: a std::stack
 * cannot be popped while something is walking it, and here the thing being popped is
 * routinely the thing doing the popping.
 *
 * Written against integers. If a rule cannot be stated about integers, it was never a
 * rule about ordering -- it was a rule about creatures, and it belongs somewhere else.
 */

#include "TestHarness.h"

#include "Roster.h"

#include <vector>

using Helm::Roster;

// ---------------------------------------------------------------------------------
// Who drives.
// ---------------------------------------------------------------------------------

/// The newest entry drives. That is what the stack meant, and therefore what every
/// creature does today -- stated here so that the day it becomes "the most important
/// entry drives", exactly one test changes and it is this one.
TEST(Roster_TheNewestEntryDrives)
{
    Roster<int> r;
    CHECK(r.Empty());

    r.Add(10);
    CHECK(!r.Empty());
    CHECK_EQ(r.Active(), 10);

    r.Add(20);
    CHECK_EQ(r.Active(), 20);
    CHECK_EQ(r.Size(), std::size_t(2));

    r.RemoveActive();
    CHECK_EQ(r.Active(), 10);
}

// ---------------------------------------------------------------------------------
// Ranks.
// ---------------------------------------------------------------------------------

/**
 * A uniform roster behaves exactly as the stack it replaces.
 *
 * This is what let ranks land without changing behaviour: give everything the same rank
 * and "highest wins, newest among equals" degenerates to "newest wins". Every case above
 * exercises that path, which is why they were written before ranks existed and did not
 * have to change when ranks arrived.
 */
TEST(Roster_UniformRanksDegenerateToAStack)
{
    Roster<int> r;
    r.Add(1, Helm::Rank::Combat);
    r.Add(2, Helm::Rank::Combat);
    r.Add(3, Helm::Rank::Combat);

    CHECK_EQ(r.Active(), 3);
    r.RemoveActive();
    CHECK_EQ(r.Active(), 2);
}

/// The highest rank drives, whatever the order of arrival.
TEST(Roster_TheHighestRankDrives)
{
    Roster<int> r;
    r.Add(10, Helm::Rank::Routine);   // a patrol
    r.Add(20, Helm::Rank::Panic);     // feared
    r.Add(30, Helm::Rank::Combat);    // told to chase, AFTER the fear

    // Under the old rule the chase arrived last and took over, and the creature
    // pursued its target while feared. Losing control of yourself outranks it.
    CHECK_EQ(r.Active(), 20);
    CHECK(r.ActiveRank() == Helm::Rank::Panic);

    // When the fear ends the chase takes over -- it was never lost, only covered.
    r.RemoveActive();
    CHECK_EQ(r.Active(), 30);
    CHECK(r.ActiveRank() == Helm::Rank::Combat);

    r.RemoveActive();
    CHECK_EQ(r.Active(), 10);
}

/// Removal takes the DRIVING entry, which is no longer necessarily the last added.
TEST(Roster_RemovalTakesTheDriverNotTheLast)
{
    Roster<int> r;
    r.Add(1, Helm::Rank::Routine);
    r.Add(2, Helm::Rank::Panic);
    r.Add(3, Helm::Rank::Errand);

    CHECK_EQ(r.Active(), 2);
    r.RemoveActive();

    // 3 is still there: it was added after 2 and outlived it.
    CHECK_EQ(r.Size(), std::size_t(2));
    CHECK_EQ(r.Active(), 3);

    const std::vector<int> freed = r.TakeRetired();
    CHECK_EQ(freed.size(), std::size_t(1));
    CHECK_EQ(freed[0], 2);
}

/// Among equals, the newest still wins -- so a second chase supersedes the first.
TEST(Roster_RecencyBreaksTiesWithinARank)
{
    Roster<int> r;
    r.Add(1, Helm::Rank::Routine);
    r.Add(2, Helm::Rank::Combat);
    r.Add(3, Helm::Rank::Combat);
    CHECK_EQ(r.Active(), 3);

    // And a lower rank added afterwards does not steal the wheel.
    r.Add(4, Helm::Rank::Errand);
    CHECK_EQ(r.Active(), 3);
}

/// Oldest first, so the bottom of the roster is the unit's default behaviour.
TEST(Roster_IteratesOldestFirst)
{
    Roster<int> r;
    r.Add(1);
    r.Add(2);
    r.Add(3);

    std::vector<int> seen;
    for (int v : r)
    {
        seen.push_back(v);
    }
    CHECK_EQ(seen.size(), std::size_t(3));
    CHECK_EQ(seen[0], 1);
    CHECK_EQ(seen[2], 3);
}

// ---------------------------------------------------------------------------------
// The floor.
// ---------------------------------------------------------------------------------

/**
 * A unit always has something driving it. The old code said so four times, as
 * `size() > 1` in four functions; saying it once, as a floor, is the difference
 * between a rule and a habit.
 */
TEST(Roster_TheFloorIsNeverRemoved)
{
    Roster<int> r;
    r.Add(1);

    CHECK(!r.RemoveActive());
    CHECK_EQ(r.Size(), std::size_t(1));
    CHECK_EQ(r.Active(), 1);

    r.Add(2);
    CHECK(r.RemoveActive());
    CHECK(!r.RemoveActive());
    CHECK_EQ(r.Active(), 1);
}

TEST(Roster_RemoveAboveStopsAtTheFloor)
{
    Roster<int> r;
    for (int i = 1; i <= 5; ++i)
    {
        r.Add(i);
    }

    CHECK_EQ(r.RemoveAbove(1), std::size_t(4));
    CHECK_EQ(r.Size(), std::size_t(1));
    CHECK_EQ(r.Active(), 1);

    // Already at the floor: nothing to do, and nothing that says otherwise.
    CHECK_EQ(r.RemoveAbove(1), std::size_t(0));

    // A floor of zero is how teardown clears the lot.
    r.Add(7);
    CHECK_EQ(r.RemoveAbove(0), std::size_t(2));
    CHECK(r.Empty());
}

// ---------------------------------------------------------------------------------
// Removal while driving.
// ---------------------------------------------------------------------------------

/**
 * The reason none of this could be a plain stack.
 *
 * A generator's Update asks the master to expire it -- so the object being removed is
 * the one whose call stack we are standing in, and freeing it there frees the frame we
 * are about to return through. The old code answered this with a pair of flag bits and
 * a side list; the rule underneath is just that removal and release are two events.
 */
TEST(Roster_RemovalRetainsUntilTheCallerReleases)
{
    Roster<int> r;
    r.Add(1);
    r.Add(2);
    r.Add(3);

    r.BeginDriving();
    CHECK(r.Driving());
    r.RemoveActive();

    // Gone from the roster the moment it is removed -- nothing else may pick it up.
    CHECK_EQ(r.Active(), 2);
    CHECK_EQ(r.Size(), std::size_t(2));

    // But still held, because we are standing in it.
    CHECK(r.HasRetired());
    CHECK_EQ(r.Retired().size(), std::size_t(1));
    CHECK_EQ(r.Retired()[0], 3);

    r.EndDriving();
    CHECK(!r.Driving());

    const std::vector<int> freed = r.TakeRetired();
    CHECK_EQ(freed.size(), std::size_t(1));
    CHECK_EQ(freed[0], 3);
    CHECK(!r.HasRetired());
}

/**
 * A replacement added during a removal is NOT removed by it.
 *
 * Finalize is allowed to push a new generator -- a creature that stops fleeing goes
 * home, and it says so from inside the cleanup of the flee. The old code guarded this
 * with a comment ("Store current top MMGen, as Finalize might push a new MMGen") and a
 * re-read of top(); here it is a property of the order of operations, so the guard has
 * nothing to be forgotten in.
 */
TEST(Roster_AnEntryAddedDuringRemovalSurvivesIt)
{
    Roster<int> r;
    r.Add(1);
    r.Add(2);

    r.BeginDriving();
    r.RemoveActive();          // 2 comes off; imagine its cleanup running now
    r.Add(99);                 // ...and pushing a replacement
    r.EndDriving();

    CHECK_EQ(r.Active(), 99);
    CHECK_EQ(r.Size(), std::size_t(2));

    // The replacement is not in the retired list. Freeing that list must not free it.
    const std::vector<int> freed = r.TakeRetired();
    CHECK_EQ(freed.size(), std::size_t(1));
    CHECK_EQ(freed[0], 2);
}

/// Nested driving: a generator whose cleanup drives the roster again must not let the
/// inner completion release what the outer call is still standing in.
TEST(Roster_DrivingNests)
{
    Roster<int> r;
    r.Add(1);
    r.Add(2);
    r.Add(3);

    r.BeginDriving();
    r.BeginDriving();
    r.RemoveActive();
    r.EndDriving();
    CHECK(r.Driving());        // still inside the outer call
    r.EndDriving();
    CHECK(!r.Driving());
}

// ---------------------------------------------------------------------------------
// The chase/follow rule.
// ---------------------------------------------------------------------------------

/**
 * Expiring the active entry also drops the ones directly beneath it that are of the
 * same kind.
 *
 * Chase and follow stack on one another, so expiring the top left another chase
 * underneath and the creature carried on pursuing something it had just been told to
 * stop pursuing. The old code did this inline, inside expire, as a loop testing two
 * enum values; as a predicate the rule is visible and the enum values stay where they
 * belong.
 */
TEST(Roster_RemovalCanTakeItsOwnKindWithIt)
{
    // Even numbers stand for "targeted" here; the roster does not care which is which.
    Roster<int> r;
    r.Add(1);       // the default, at the floor
    r.Add(2);       // targeted
    r.Add(4);       // targeted
    r.Add(6);       // targeted, and active

    auto targeted = [](int v) { return (v % 2) == 0; };
    CHECK_EQ(r.RemoveActiveAnd(targeted), std::size_t(3));
    CHECK_EQ(r.Size(), std::size_t(1));
    CHECK_EQ(r.Active(), 1);

    // It stops at the first entry the predicate rejects.
    Roster<int> s;
    s.Add(1);
    s.Add(3);       // not targeted
    s.Add(2);       // targeted, active
    CHECK_EQ(s.RemoveActiveAnd(targeted), std::size_t(1));
    CHECK_EQ(s.Active(), 3);

    // And it never eats through the floor, however well the predicate matches.
    Roster<int> t;
    t.Add(2);
    t.Add(4);
    CHECK_EQ(t.RemoveActiveAnd(targeted), std::size_t(1));
    CHECK_EQ(t.Size(), std::size_t(1));
    CHECK_EQ(t.Active(), 2);
}

/// Teardown drops everything and retires nothing: the payloads are about to be disposed
/// of wholesale, and a retired list at that point is a list of things freed twice.
TEST(Roster_AbandonRetiresNothing)
{
    Roster<int> r;
    r.Add(1);
    r.Add(2);
    r.BeginDriving();
    r.RemoveActive();
    CHECK(r.HasRetired());

    r.Abandon();
    CHECK(r.Empty());
    CHECK(!r.HasRetired());
    CHECK(!r.Driving());
}
