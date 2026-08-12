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
 * @file AuraTypeIndexTest.cpp
 * @brief Cases for the intrusive per-aura-type chains.
 *
 * An intrusive list is easy to write and easy to get subtly wrong: the failures
 * are a lost head on removing the first element, a dangling prev on removing the
 * last, and an occupancy bit that stays set after the chain empties. Each of
 * those is silent -- the wrong answer is a missing aura, not a crash -- so each
 * gets a case here.
 *
 * The element is a stand-in. A real Aura cannot be built in this binary, which
 * is why AuraTypeIndex is a template over the element rather than hard-wired to
 * one.
 */

#include "TestHarness.h"

#include "AuraTypeIndex.h"

#include <algorithm>
#include <vector>

namespace
{
    /// Carries the two links and the indexed flag, exactly as Aura will.
    class FakeAura
    {
        public:
            explicit FakeAura(uint32 id) : m_id(id) {}

            uint32 Id() const { return m_id; }

            FakeAura* GetNextOfAuraType() const { return m_next; }
            void SetNextOfAuraType(FakeAura* a) { m_next = a; }
            FakeAura* GetPrevOfAuraType() const { return m_prev; }
            void SetPrevOfAuraType(FakeAura* a) { m_prev = a; }
            bool GetAuraTypeIndexed() const { return m_indexed; }
            void SetAuraTypeIndexed(bool state) { m_indexed = state; }

        private:
            uint32 m_id;
            FakeAura* m_next = nullptr;
            FakeAura* m_prev = nullptr;
            bool m_indexed = false;
    };

    typedef AuraTypeIndex<FakeAura> Index;

    std::vector<uint32> IdsOf(Index const& index, AuraType type)
    {
        std::vector<uint32> ids;
        AuraChainRange<FakeAura> range = index.Get(type);
        for (AuraChainRange<FakeAura>::const_iterator it = range.begin();
             it != range.end(); ++it)
        {
            ids.push_back((*it)->Id());
        }
        return ids;
    }

    /// Walks the chain backwards from the tail; must mirror the forward walk.
    std::vector<uint32> ReverseIdsOf(Index const& index, AuraType type)
    {
        AuraChainRange<FakeAura> range = index.Get(type);
        FakeAura* tail = nullptr;
        for (AuraChainRange<FakeAura>::const_iterator it = range.begin();
             it != range.end(); ++it)
        {
            tail = *it;
        }

        std::vector<uint32> ids;
        for (FakeAura* it = tail; it; it = it->GetPrevOfAuraType())
        {
            ids.push_back(it->Id());
        }
        std::reverse(ids.begin(), ids.end());
        return ids;
    }

    const AuraType T1 = SPELL_AURA_MOD_DAMAGE_DONE;
    const AuraType T2 = SPELL_AURA_MOD_RESISTANCE;
}

TEST(AuraTypeIndex_EmptyIndexHasNothing)
{
    Index index;

    CHECK(!index.Has(T1));
    CHECK(index.Get(T1).empty());
    CHECK_EQ(uint32(index.Get(T1).size()), 0u);
    CHECK(index.Get(T1).front() == nullptr);
}

TEST(AuraTypeIndex_OutOfRangeTypeIsRejectedNotIndexed)
{
    // A modifier whose auraname is out of range must not write past the array.
    Index index;
    FakeAura a(1);

    index.Add(&a, AuraType(TOTAL_AURAS));
    CHECK(!a.GetAuraTypeIndexed());
    CHECK(!index.Has(AuraType(TOTAL_AURAS)));
    CHECK(index.Get(AuraType(TOTAL_AURAS)).empty());

    index.Add(&a, AuraType(TOTAL_AURAS + 9000));
    CHECK(!a.GetAuraTypeIndexed());
}

TEST(AuraTypeIndex_AddToBackKeepsApplicationOrder)
{
    // Several handlers take front() and the accumulators sum in chain order, so
    // the order auras were applied in is observable and must survive.
    Index index;
    FakeAura a(1), b(2), c(3);

    index.AddToBack(&a, T1);
    index.AddToBack(&b, T1);
    index.AddToBack(&c, T1);

    CHECK(index.Has(T1));
    CHECK_EQ(uint32(index.Get(T1).size()), 3u);
    CHECK(IdsOf(index, T1) == std::vector<uint32>({1, 2, 3}));
    CHECK(ReverseIdsOf(index, T1) == std::vector<uint32>({1, 2, 3}));
    CHECK_EQ(index.Get(T1).front()->Id(), 1u);
}

TEST(AuraTypeIndex_AddPutsTheNewestFirst)
{
    Index index;
    FakeAura a(1), b(2), c(3);

    index.Add(&a, T1);
    index.Add(&b, T1);
    index.Add(&c, T1);

    CHECK(IdsOf(index, T1) == std::vector<uint32>({3, 2, 1}));
    CHECK(ReverseIdsOf(index, T1) == std::vector<uint32>({3, 2, 1}));
}

TEST(AuraTypeIndex_RemoveHeadKeepsTheRestReachable)
{
    // The classic intrusive-list bug: unlinking the first element loses the head.
    Index index;
    FakeAura a(1), b(2), c(3);
    index.AddToBack(&a, T1);
    index.AddToBack(&b, T1);
    index.AddToBack(&c, T1);

    index.Remove(&a, T1);

    CHECK(IdsOf(index, T1) == std::vector<uint32>({2, 3}));
    CHECK(ReverseIdsOf(index, T1) == std::vector<uint32>({2, 3}));
    CHECK(index.Has(T1));
    CHECK(!a.GetAuraTypeIndexed());
    CHECK(a.GetNextOfAuraType() == nullptr);
    CHECK(a.GetPrevOfAuraType() == nullptr);
}

TEST(AuraTypeIndex_RemoveTailLeavesNoDanglingLink)
{
    Index index;
    FakeAura a(1), b(2), c(3);
    index.AddToBack(&a, T1);
    index.AddToBack(&b, T1);
    index.AddToBack(&c, T1);

    index.Remove(&c, T1);

    CHECK(IdsOf(index, T1) == std::vector<uint32>({1, 2}));
    CHECK(ReverseIdsOf(index, T1) == std::vector<uint32>({1, 2}));
    CHECK(b.GetNextOfAuraType() == nullptr);
}

TEST(AuraTypeIndex_RemoveMiddleClosesTheChainBothWays)
{
    Index index;
    FakeAura a(1), b(2), c(3);
    index.AddToBack(&a, T1);
    index.AddToBack(&b, T1);
    index.AddToBack(&c, T1);

    index.Remove(&b, T1);

    CHECK(IdsOf(index, T1) == std::vector<uint32>({1, 3}));
    CHECK(ReverseIdsOf(index, T1) == std::vector<uint32>({1, 3}));
    CHECK(a.GetNextOfAuraType() == &c);
    CHECK(c.GetPrevOfAuraType() == &a);
}

TEST(AuraTypeIndex_OccupancyBitClearsWhenTheChainEmpties)
{
    // A bit left set makes Has() lie, and every caller that trusts it skips work
    // it should have done -- or does work it should have skipped.
    Index index;
    FakeAura a(1), b(2);

    index.AddToBack(&a, T1);
    index.AddToBack(&b, T1);
    CHECK(index.Has(T1));

    index.Remove(&a, T1);
    CHECK(index.Has(T1));

    index.Remove(&b, T1);
    CHECK(!index.Has(T1));
    CHECK(index.Get(T1).empty());
}

TEST(AuraTypeIndex_TypesDoNotLeakIntoEachOther)
{
    Index index;
    FakeAura a(1), b(2);

    index.AddToBack(&a, T1);
    index.AddToBack(&b, T2);

    CHECK(index.Has(T1));
    CHECK(index.Has(T2));
    CHECK(IdsOf(index, T1) == std::vector<uint32>({1}));
    CHECK(IdsOf(index, T2) == std::vector<uint32>({2}));

    index.Remove(&a, T1);
    CHECK(!index.Has(T1));
    CHECK(index.Has(T2));
    CHECK(IdsOf(index, T2) == std::vector<uint32>({2}));
}

TEST(AuraTypeIndex_OccupancyBitsetSpansEveryWord)
{
    // 262 types is five 64-bit words. A shift or word-index slip shows at the
    // boundaries, not in the middle.
    const uint32 probes[] = {0, 63, 64, 127, 128, 191, 192, 255, TOTAL_AURAS - 1};

    for (uint32 probe : probes)
    {
        Index index;
        FakeAura a(1);
        const AuraType type = AuraType(probe);

        index.AddToBack(&a, type);
        CHECK(index.Has(type));

        // No other type may have been marked occupied.
        uint32 occupied = 0;
        for (uint32 t = 0; t < TOTAL_AURAS; ++t)
        {
            if (index.Has(AuraType(t)))
            {
                ++occupied;
                CHECK_EQ(t, probe);
            }
        }
        CHECK_EQ(occupied, 1u);

        index.Remove(&a, type);
        CHECK(!index.Has(type));
    }
}

TEST(AuraTypeIndex_DoubleAddAndStrayRemoveAreIgnored)
{
    Index index;
    FakeAura a(1), stray(2);

    index.AddToBack(&a, T1);
    index.AddToBack(&a, T1);                                // already indexed
    CHECK_EQ(uint32(index.Get(T1).size()), 1u);

    index.Remove(&stray, T1);                               // never indexed
    CHECK_EQ(uint32(index.Get(T1).size()), 1u);
    CHECK(IdsOf(index, T1) == std::vector<uint32>({1}));

    index.Remove(&a, T1);
    index.Remove(&a, T1);                                   // already gone
    CHECK(index.Get(T1).empty());
    CHECK(!index.Has(T1));
}

TEST(AuraTypeIndex_ClearResetsElementsAsWellAsChains)
{
    // Clear must leave the elements usable: a stale indexed flag would make a
    // later Add() silently do nothing.
    Index index;
    FakeAura a(1), b(2), c(3);
    index.AddToBack(&a, T1);
    index.AddToBack(&b, T1);
    index.AddToBack(&c, T2);

    index.Clear();

    CHECK(!index.Has(T1));
    CHECK(!index.Has(T2));
    CHECK(index.Get(T1).empty());

    for (FakeAura const* e : {&a, &b, &c})
    {
        CHECK(!e->GetAuraTypeIndexed());
        CHECK(e->GetNextOfAuraType() == nullptr);
        CHECK(e->GetPrevOfAuraType() == nullptr);
    }

    index.AddToBack(&a, T1);
    CHECK(IdsOf(index, T1) == std::vector<uint32>({1}));
}

TEST(AuraTypeIndex_SurvivesRandomAddRemoveSequences)
{
    // The property: the chain always holds exactly what was added and not yet
    // removed, in insertion order, and walks the same backwards as forwards.
    uint32 state = 12345;
    auto next = [&state]()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };

    const uint32 POOL = 24;
    std::vector<FakeAura> pool;
    pool.reserve(POOL);
    for (uint32 i = 0; i < POOL; ++i)
    {
        pool.push_back(FakeAura(i));
    }

    Index index;
    std::vector<uint32> expected;

    for (uint32 step = 0; step < 4000; ++step)
    {
        const uint32 pick = next() % POOL;
        FakeAura& e = pool[pick];

        if (!e.GetAuraTypeIndexed())
        {
            index.AddToBack(&e, T1);
            expected.push_back(pick);
        }
        else
        {
            index.Remove(&e, T1);
            expected.erase(std::find(expected.begin(), expected.end(), pick));
        }

        if (IdsOf(index, T1) != expected)
        {
            testing::ReportFailure(__FILE__, __LINE__,
                "step " + std::to_string(step) + ": forward chain diverged");
            return;
        }

        if (ReverseIdsOf(index, T1) != expected)
        {
            testing::ReportFailure(__FILE__, __LINE__,
                "step " + std::to_string(step) + ": backward chain diverged");
            return;
        }

        if (index.Has(T1) != !expected.empty())
        {
            testing::ReportFailure(__FILE__, __LINE__,
                "step " + std::to_string(step) + ": occupancy bit disagrees");
            return;
        }
    }
}
