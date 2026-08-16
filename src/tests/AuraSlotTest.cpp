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
 * Which visible slot an aura gets.
 *
 * The first aura rule that can be asked a question without a server. The
 * interesting cases are all at the two seams -- where the positive band stops,
 * and what a full band answers -- and neither could be reached before without
 * standing up a player and buffing them forty times.
 */

#include "TestHarness.h"

#include "Aura/pure/SlotAllocator.h"

#include <cstdint>
#include <vector>

namespace
{
    // The 2.4.3 numbers, written here rather than included: the header they
    // live in knows about update fields, and this test links without the game.
    enum : std::uint8_t { Total = 56, PositiveEnd = 40 };

    std::vector<std::uint32_t> Empty()
    {
        return std::vector<std::uint32_t>(Total, 0u);
    }
}

TEST(AuraSlotPositiveTakesTheFirstBand)
{
    std::vector<std::uint32_t> slots = Empty();

    CHECK(aura::AllocateSlot(true, slots.data(), Total, PositiveEnd) == 0);
}

TEST(AuraSlotNegativeStartsWhereThePositiveBandEnds)
{
    std::vector<std::uint32_t> slots = Empty();

    // Not slot 0. A negative aura in the positive band is a debuff drawn among
    // the buffs, which is the client's business and not ours to reinterpret.
    CHECK(aura::AllocateSlot(false, slots.data(), Total, PositiveEnd) == 40);
}

TEST(AuraSlotTakesTheFirstHoleRatherThanTheEnd)
{
    std::vector<std::uint32_t> slots = Empty();
    slots[0] = 1234;
    slots[1] = 5678;
    slots[3] = 9012;

    // Two is free and three is taken; a scan that appended instead of filling
    // would answer four and waste the hole.
    CHECK(aura::AllocateSlot(true, slots.data(), Total, PositiveEnd) == 2);
}

TEST(AuraSlotPositiveBandDoesNotSpillIntoTheNegativeOne)
{
    std::vector<std::uint32_t> slots = Empty();
    for (std::uint8_t at = 0; at < PositiveEnd; ++at)
    {
        slots[at] = 1;
    }

    // Forty buffs and sixteen free negative slots, and the answer is still
    // "no room". The bands are what the client reads; borrowing across them
    // would draw a buff where a debuff belongs.
    CHECK(aura::AllocateSlot(true, slots.data(), Total, PositiveEnd) ==
          aura::NoSlot);
}

TEST(AuraSlotNegativeBandDoesNotSpillIntoThePositiveOne)
{
    std::vector<std::uint32_t> slots = Empty();
    for (std::uint8_t at = PositiveEnd; at < Total; ++at)
    {
        slots[at] = 1;
    }

    CHECK(aura::AllocateSlot(false, slots.data(), Total, PositiveEnd) ==
          aura::NoSlot);
}

TEST(AuraSlotExhaustionIsTheSentinelAndNotTheTotal)
{
    std::vector<std::uint32_t> slots(Total, 1u);

    // 0xFF, not 56. The holder carries the total to mean "not offered a slot
    // yet"; answering it here would make an aura the client had no room for
    // indistinguishable from one that was never presented.
    CHECK(aura::AllocateSlot(true, slots.data(), Total, PositiveEnd) == 0xFF);
    CHECK(aura::NoSlot == 0xFF);
    CHECK(aura::NoSlot != Total);
}

TEST(AuraSlotLastSlotOfEachBandIsReachable)
{
    std::vector<std::uint32_t> slots = Empty();
    for (std::uint8_t at = 0; at < Total; ++at)
    {
        slots[at] = 1;
    }

    slots[PositiveEnd - 1] = 0;
    CHECK(aura::AllocateSlot(true, slots.data(), Total, PositiveEnd) == 39);

    slots[PositiveEnd - 1] = 1;
    slots[Total - 1] = 0;
    CHECK(aura::AllocateSlot(false, slots.data(), Total, PositiveEnd) == 55);
}

TEST(AuraSlotRefusesBandsThatDoNotFit)
{
    std::vector<std::uint32_t> slots = Empty();

    // The two numbers the wrong way round. Scanning anyway walks off the end
    // of the array, which is a crash somewhere else entirely.
    CHECK(aura::AllocateSlot(true, slots.data(), PositiveEnd, Total) ==
          aura::NoSlot);
    CHECK(aura::AllocateSlot(false, slots.data(), PositiveEnd, Total) ==
          aura::NoSlot);
}

TEST(AuraSlotRefusesNothingToScan)
{
    std::vector<std::uint32_t> slots = Empty();

    CHECK(aura::AllocateSlot(true, nullptr, Total, PositiveEnd) ==
          aura::NoSlot);
    CHECK(aura::AllocateSlot(true, slots.data(), 0, 0) == aura::NoSlot);
}
