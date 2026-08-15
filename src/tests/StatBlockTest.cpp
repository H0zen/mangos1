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
 * How a unit's stats are assembled.
 *
 * Every stat, resistance, power pool, attack power and weapon damage in the
 * game comes out of the four slots below and one formula over them, so an
 * error here is an error in all of them at once.
 */

#include "TestHarness.h"

#include "StatBlock.h"

namespace
{
    bool Near(float value, float expected)
    {
        const float difference = value - expected;
        return difference < 0.001f && difference > -0.001f;
    }
}

TEST(StatBlockStartsNeutral)
{
    StatBlock block;

    // The two that add start at zero and the two that multiply start at one,
    // so an untouched group is exactly what the unit was created with.
    CHECK(Near(block.Combine(UNIT_MOD_STAT_STRENGTH, 120.0f), 120.0f));
    CHECK(Near(block.Raw(UNIT_MOD_STAT_STRENGTH, BASE_VALUE), 0.0f));
    CHECK(Near(block.Raw(UNIT_MOD_STAT_STRENGTH, BASE_PCT), 1.0f));
    CHECK(Near(block.Raw(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE), 0.0f));
    CHECK(Near(block.Raw(UNIT_MOD_STAT_STRENGTH, TOTAL_PCT), 1.0f));
}

TEST(StatBlockCombinesInOrder)
{
    StatBlock block;

    block.Apply(UNIT_MOD_STAT_AGILITY, BASE_VALUE, 10.0f, true);
    block.Apply(UNIT_MOD_STAT_AGILITY, BASE_PCT, 100.0f, true);   // x2
    block.Apply(UNIT_MOD_STAT_AGILITY, TOTAL_VALUE, 5.0f, true);
    block.Apply(UNIT_MOD_STAT_AGILITY, TOTAL_PCT, 10.0f, true);   // x1.1

    // ((100 + 10) * 2 + 5) * 1.1
    CHECK(Near(block.Combine(UNIT_MOD_STAT_AGILITY, 100.0f), 247.5f));
}

TEST(StatBlockApplyAndRemoveRoundTrip)
{
    StatBlock block;

    for (int i = 0; i < 4; ++i)
    {
        block.Apply(UNIT_MOD_ATTACK_POWER, BASE_VALUE, 37.0f, true);
        block.Apply(UNIT_MOD_ATTACK_POWER, TOTAL_PCT, 15.0f, true);
    }

    for (int i = 0; i < 4; ++i)
    {
        block.Apply(UNIT_MOD_ATTACK_POWER, BASE_VALUE, 37.0f, false);
        block.Apply(UNIT_MOD_ATTACK_POWER, TOTAL_PCT, 15.0f, false);
    }

    CHECK(Near(block.Combine(UNIT_MOD_ATTACK_POWER, 200.0f), 200.0f));
}

TEST(StatBlockMinusHundredPercentIsReversible)
{
    StatBlock block;

    // A -100% multiplier would be zero, and removing it would divide by it.
    // The clamp is what keeps the round trip finite.
    block.Apply(UNIT_MOD_STAT_STAMINA, TOTAL_PCT, -100.0f, true);
    CHECK(Near(block.Combine(UNIT_MOD_STAT_STAMINA, 500.0f), 0.0f));

    block.Apply(UNIT_MOD_STAT_STAMINA, TOTAL_PCT, -100.0f, false);
    CHECK(Near(block.Combine(UNIT_MOD_STAT_STAMINA, 500.0f), 500.0f));
}

TEST(StatBlockNegativeTotalPercentReadsAsZero)
{
    StatBlock block;

    block.Apply(UNIT_MOD_ARMOR, TOTAL_PCT, -150.0f, true);

    // Off, not negative: a negative multiplier would otherwise turn a
    // negative armour value into a positive one.
    CHECK(Near(block.Value(UNIT_MOD_ARMOR, TOTAL_PCT), 0.0f));
    CHECK(Near(block.Combine(UNIT_MOD_ARMOR, 4000.0f), 0.0f));
}

TEST(StatBlockOnlyTotalPercentIsClampedOnRead)
{
    StatBlock block;

    // The other three report what they hold, negative or not. Only the last
    // multiplier decides whether the group is on at all.
    block.Set(UNIT_MOD_STAT_SPIRIT, BASE_VALUE, -30.0f);
    block.Set(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE, -7.0f);

    CHECK(Near(block.Value(UNIT_MOD_STAT_SPIRIT, BASE_VALUE), -30.0f));
    CHECK(Near(block.Value(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE), -7.0f));
}

TEST(StatBlockRefusesAnIndexOutOfRange)
{
    StatBlock block;

    CHECK(!block.Apply(UNIT_MOD_END, BASE_VALUE, 1.0f, true));
    CHECK(!block.Apply(UNIT_MOD_ARMOR, MODIFIER_TYPE_END, 1.0f, true));
    CHECK(!StatBlock::InRange(UNIT_MOD_END, BASE_VALUE));
    CHECK(StatBlock::InRange(UNIT_MOD_DAMAGE_RANGED, TOTAL_PCT));

    CHECK(Near(block.Value(UNIT_MOD_END, BASE_VALUE), 0.0f));
    CHECK(Near(block.Combine(UNIT_MOD_END, 100.0f), 0.0f));
}

TEST(StatBlockGroupsAreIndependent)
{
    StatBlock block;

    block.Apply(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE, 50.0f, true);

    CHECK(Near(block.Combine(UNIT_MOD_STAT_STRENGTH, 0.0f), 50.0f));
    CHECK(Near(block.Combine(UNIT_MOD_STAT_AGILITY, 0.0f), 0.0f));
}

TEST(StatBlockOffhandHalvingIsAPlainSet)
{
    // What Unit does at construction, and the one value that does not start
    // neutral.
    StatBlock block;
    block.Set(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_PCT, 0.5f);

    CHECK(Near(block.Combine(UNIT_MOD_DAMAGE_OFFHAND, 100.0f), 50.0f));
}
