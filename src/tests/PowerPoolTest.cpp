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

/**
 * The resource tick: when it fires, and what the pool holds afterwards.
 *
 * Both halves are unsigned arithmetic near a boundary -- a countdown that can
 * be handed more time than it has left, and a pool that can be pushed past its
 * maximum or below zero -- so the edges are the whole test.
 */

#include "TestHarness.h"

#include "PowerPool.h"

#include <limits>

TEST(PowerPoolClockSaturatesAtZero)
{
    PowerPool pool(500);

    // An update longer than what is left must land on zero. Subtracting would
    // wrap to four billion milliseconds and the unit would never regenerate
    // again.
    pool.Advance(2000);

    CHECK(pool.UntilTick() == 0);
    CHECK(pool.TickDue());
}

TEST(PowerPoolClockCountsDownAndRearms)
{
    PowerPool pool(REGEN_TIME_FULL);

    pool.Advance(800);
    CHECK(pool.UntilTick() == 1200);
    CHECK(!pool.TickDue());

    pool.Advance(1200);
    CHECK(pool.TickDue());

    pool.ArmTick(REGEN_TIME_FULL);
    CHECK(!pool.TickDue());
    CHECK(pool.UntilTick() == 2000);
}

TEST(PowerPoolStartsDue)
{
    // A default-constructed pool owes a tick immediately; a unit that wants to
    // wait says so by arming.
    PowerPool pool;
    CHECK(pool.TickDue());
}

TEST(PowerPoolFillStopsAtMaximum)
{
    RegenTick tick;
    tick.current = 90;
    tick.maximum = 100;
    tick.base    = 40.0f;

    CHECK(RegeneratedValue(tick) == 100);
}

TEST(PowerPoolFillCannotOverflow)
{
    RegenTick tick;
    tick.current = std::numeric_limits<std::uint32_t>::max() - 1;
    tick.maximum = std::numeric_limits<std::uint32_t>::max();
    tick.base    = 1000.0f;

    // current + step would wrap past the maximum and read as an empty pool.
    CHECK(RegeneratedValue(tick) == std::numeric_limits<std::uint32_t>::max());
}

TEST(PowerPoolDrainStopsAtZero)
{
    RegenTick tick;
    tick.behaviour = PoolBehaviour::Drains;
    tick.current   = 15;
    tick.maximum   = 1000;
    tick.base      = 20.0f;

    CHECK(RegeneratedValue(tick) == 0);
}

TEST(PowerPoolDrainSubtracts)
{
    RegenTick tick;
    tick.behaviour = PoolBehaviour::Drains;
    tick.current   = 500;
    tick.maximum   = 1000;
    tick.base      = 20.0f;

    CHECK(RegeneratedValue(tick) == 480);
}

TEST(PowerPoolPercentScalesTheWholeAmount)
{
    RegenTick tick;
    tick.current   = 0;
    tick.maximum   = 1000;
    tick.base      = 20.0f;
    tick.flatBonus = 5.0f;
    tick.percent   = 2.0f;

    // The flat bonus is inside the multiplier, not beside it.
    CHECK(RegeneratedValue(tick) == 50);
}

TEST(PowerPoolNegativeAmountMovesNothing)
{
    RegenTick tick;
    tick.current   = 400;
    tick.maximum   = 1000;
    tick.base      = 20.0f;
    tick.flatBonus = -60.0f;

    // A pool is unsigned and has nowhere to put a negative step; filling by it
    // would wrap the pool to its maximum.
    CHECK(RegeneratedValue(tick) == 400);

    tick.behaviour = PoolBehaviour::Drains;
    CHECK(RegeneratedValue(tick) == 400);
}

TEST(PowerPoolFullPoolStaysFull)
{
    RegenTick tick;
    tick.current = 250;
    tick.maximum = 200;
    tick.base    = 10.0f;

    // Above the maximum after a debuff dropped it: the tick brings it down to
    // the maximum rather than adding to it.
    CHECK(RegeneratedValue(tick) == 200);
}
