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
 * How a movement rate is assembled.
 *
 * Speed is the one number a player watches every second of play, and it comes
 * out of eight factors combined in an order that is not commutative: a
 * normalisation ceiling applied after a slow gives a different answer than one
 * applied before it.
 */

#include "TestHarness.h"

#include "SpeedSet.h"

namespace
{
    bool Near(float value, float expected)
    {
        const float difference = value - expected;
        return difference < 0.001f && difference > -0.001f;
    }
}

TEST(SpeedSetStartsAtTheTableSpeed)
{
    SpeedSet speeds;

    CHECK(Near(speeds.Rate(MOVE_RUN), 1.0f));
    CHECK(Near(speeds.Speed(MOVE_RUN), 7.0f));
    CHECK(Near(speeds.Speed(MOVE_WALK), 2.5f));
}

TEST(SpeedSetReportsOnlyRealChanges)
{
    SpeedSet speeds;

    CHECK(!speeds.Store(MOVE_RUN, 1.0f));       // already that
    CHECK(speeds.Store(MOVE_RUN, 1.4f));
    CHECK(!speeds.Store(MOVE_RUN, 1.4f));

    CHECK(Near(speeds.Speed(MOVE_RUN), 9.8f));
}

TEST(SpeedSetClampsNegativeToStandingStill)
{
    SpeedSet speeds;

    CHECK(speeds.Store(MOVE_RUN, -3.0f));
    CHECK(Near(speeds.Rate(MOVE_RUN), 0.0f));

    // Already stopped, so a second negative rate changes nothing.
    CHECK(!speeds.Store(MOVE_RUN, -9.0f));
}

TEST(SpeedRateWithNothingOnIsUnchanged)
{
    SpeedFactors factors;

    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.0f));
}

TEST(SpeedRateTakesTheLargerBonusNotBoth)
{
    SpeedFactors factors;
    factors.stackBonus    = 1.3f;
    factors.nonStackBonus = 1.5f;

    // 1.95 would be both; the rule is that only the better one counts.
    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.5f));

    factors.stackBonus = 1.7f;
    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.7f));
}

TEST(SpeedRateBonusStandsAloneWithoutASpeedAura)
{
    SpeedFactors factors;
    factors.stackBonus = 1.4f;

    // With no main modifier the bonus is the answer, not a multiplier on 1.0
    // plus a percentage of nothing.
    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.4f));

    factors.mainMod = 100;
    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 2.8f));
}

TEST(SpeedRateNormalisationIsACeilingInYards)
{
    SpeedFactors factors;
    factors.mainMod       = 100;   // would be rate 2.0, i.e. 14 yards/sec
    factors.normalization = 7;     // pinned to 7 yards/sec, i.e. rate 1.0

    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.0f));

    // A rate already under the ceiling is left alone.
    factors.mainMod = 0;
    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.0f));
}

TEST(SpeedRateNormalisationSkipsTheBackwardTypes)
{
    SpeedFactors factors;
    factors.mainMod       = 100;
    factors.normalization = 1;

    // Only run, swim and flight honour the aura; walking away from it is
    // unaffected.
    CHECK(Near(ComposeSpeedRate(MOVE_WALK, factors), 2.0f));
    CHECK(Near(ComposeSpeedRate(MOVE_RUN_BACK, factors), 2.0f));
    CHECK(ComposeSpeedRate(MOVE_SWIM, factors) < 2.0f);
}

TEST(SpeedRateSlowBitesAfterTheCeiling)
{
    SpeedFactors factors;
    factors.mainMod       = 100;   // rate 2.0
    factors.normalization = 7;     // ceiling at rate 1.0
    factors.slow          = -50;

    // Ceiling first, then the slow: 1.0 * 0.5. Slowing first would have given
    // 1.0, and a pinned unit would have been immune to crippling.
    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 0.5f));
}

TEST(SpeedRateCreatureAndRatioMultiplyLast)
{
    SpeedFactors factors;
    factors.creatureRate = 1.2f;
    factors.stateScale   = 0.66f;  // went looking for help
    factors.ratio        = 0.5f;

    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.2f * 0.66f * 0.5f));
}

TEST(SpeedRateSlowOfZeroChangesNothing)
{
    SpeedFactors factors;
    factors.stackBonus = 1.5f;
    factors.slow       = 0;

    CHECK(Near(ComposeSpeedRate(MOVE_RUN, factors), 1.5f));
}
