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
 * @file ProcIndexTest.cpp
 * @brief Cases for the aggregate proc mask, against a stand-in for Unit.
 *
 * The failure that matters here is one-sided. A stale aggregate that reports
 * *more* than it should only costs a scan that finds nothing; a stale aggregate
 * that reports *less* silently swallows a proc that should have fired, and
 * nothing downstream would ever notice. So the cases below hammer the direction
 * that hurts: every add and remove, in every order, followed by a query.
 *
 * FakeUnit reproduces exactly the contract the real Unit signs -- invalidate at
 * every point where the enumerated set changes, and nowhere else is that set
 * allowed to change. In Unit that is the insert and the erase in
 * UnitAura.cpp, and grep proves those are the only two.
 */

#include "TestHarness.h"

#include "ProcIndex.h"

#include <cstdint>
#include <vector>

namespace
{
    /**
     * @brief A stand-in for Unit: holds "auras" and their effective proc flags.
     *
     * Deliberately not a Unit. The real one cannot be built in this binary --
     * it reaches the map, the session and the database -- which is the whole
     * reason ProcIndex takes an enumerator instead of walking holders itself.
     */
    class FakeUnit
    {
        public:
            void AddAura(uint32 spellId, uint32 procFlags)
            {
                m_holders.push_back(Holder{spellId, procFlags, false});
                m_procIndex.Invalidate();
            }

            /// Removes the first holder of that spell, mirroring the real erase.
            void RemoveAura(uint32 spellId)
            {
                for (std::vector<Holder>::iterator it = m_holders.begin();
                     it != m_holders.end(); ++it)
                {
                    if (it->spellId == spellId)
                    {
                        m_holders.erase(it);
                        m_procIndex.Invalidate();
                        return;
                    }
                }
            }

            /// Marks a holder deleted without erasing it; the real enumerator
            /// skips those, and doing so must not disturb the aggregate.
            void MarkDeleted(uint32 spellId)
            {
                for (Holder& h : m_holders)
                {
                    if (h.spellId == spellId)
                    {
                        h.deleted = true;
                        m_procIndex.Invalidate();
                        return;
                    }
                }
            }

            bool CanAnyAuraProcFrom(uint32 eventFlags) const
            {
                return m_procIndex.Matches(eventFlags, [this](auto const& sink)
                {
                    for (Holder const& h : m_holders)
                    {
                        if (!h.deleted)
                        {
                            sink(h.procFlags);
                        }
                    }
                });
            }

            /// What the aggregate must be, computed the slow, obvious way.
            uint32 ExpectedAggregate() const
            {
                uint32 expected = 0;
                for (Holder const& h : m_holders)
                {
                    if (!h.deleted)
                    {
                        expected |= h.procFlags;
                    }
                }
                return expected;
            }

            ProcIndex const& Index() const { return m_procIndex; }

        private:
            struct Holder
            {
                uint32 spellId;
                uint32 procFlags;
                bool deleted;
            };

            std::vector<Holder> m_holders;
            ProcIndex m_procIndex;
    };

    /// Deterministic, self-contained PRNG -- a seeded run must reproduce.
    class Rng
    {
        public:
            explicit Rng(uint32 seed) : m_state(seed ? seed : 1u) {}

            uint32 Next()
            {
                // xorshift32
                m_state ^= m_state << 13;
                m_state ^= m_state >> 17;
                m_state ^= m_state << 5;
                return m_state;
            }

            uint32 Below(uint32 bound) { return bound ? Next() % bound : 0; }

        private:
            uint32 m_state;
    };
}

TEST(ProcIndex_EmptyOwnerMatchesNothing)
{
    FakeUnit unit;

    CHECK(!unit.CanAnyAuraProcFrom(0xFFFFFFFF));
    CHECK(!unit.CanAnyAuraProcFrom(0x1));
    CHECK(!unit.CanAnyAuraProcFrom(0));
    CHECK_EQ(unit.Index().GetAggregate(), 0u);
}

TEST(ProcIndex_StartsCleanSoAnEmptyOwnerNeverRebuilds)
{
    // A fresh Unit holds nothing, and zero is already the right answer. Starting
    // dirty would make every creature in the world rebuild once for nothing.
    FakeUnit unit;
    CHECK(!unit.Index().IsDirty());

    unit.CanAnyAuraProcFrom(0xFF);
    CHECK(!unit.Index().IsDirty());
}

TEST(ProcIndex_AddMakesTheFlagsMatch)
{
    FakeUnit unit;
    unit.AddAura(100, 0x00000010);

    CHECK(unit.Index().IsDirty());
    CHECK(unit.CanAnyAuraProcFrom(0x00000010));
    CHECK(!unit.Index().IsDirty());

    CHECK_EQ(unit.Index().GetAggregate(), 0x00000010u);

    // Any overlapping bit is a match; a disjoint mask is not.
    CHECK(unit.CanAnyAuraProcFrom(0x000000FF));
    CHECK(!unit.CanAnyAuraProcFrom(0x00000020));
    CHECK(!unit.CanAnyAuraProcFrom(0));
}

TEST(ProcIndex_RemoveTakesTheFlagsBackAway)
{
    FakeUnit unit;
    unit.AddAura(100, 0x00000010);
    unit.AddAura(200, 0x00000020);

    CHECK(unit.CanAnyAuraProcFrom(0x00000010));
    CHECK(unit.CanAnyAuraProcFrom(0x00000020));
    CHECK_EQ(unit.Index().GetAggregate(), 0x00000030u);

    unit.RemoveAura(100);

    // The bit the removed aura contributed must be gone. An aggregate kept by
    // or-ing on add and never rebuilt would still report it here.
    CHECK(!unit.CanAnyAuraProcFrom(0x00000010));
    CHECK(unit.CanAnyAuraProcFrom(0x00000020));
    CHECK_EQ(unit.Index().GetAggregate(), 0x00000020u);

    unit.RemoveAura(200);
    CHECK(!unit.CanAnyAuraProcFrom(0xFFFFFFFF));
    CHECK_EQ(unit.Index().GetAggregate(), 0u);
}

TEST(ProcIndex_SharedBitSurvivesRemovalOfOneContributor)
{
    // Two auras carrying the same flag: removing one must not clear it.
    FakeUnit unit;
    unit.AddAura(100, 0x00000004);
    unit.AddAura(200, 0x00000004);

    CHECK(unit.CanAnyAuraProcFrom(0x00000004));

    unit.RemoveAura(100);
    CHECK(unit.CanAnyAuraProcFrom(0x00000004));

    unit.RemoveAura(200);
    CHECK(!unit.CanAnyAuraProcFrom(0x00000004));
}

TEST(ProcIndex_ZeroFlagHoldersContributeNothing)
{
    // Most auras have no proc flags at all. They must not make the aggregate
    // match anything, and must not stop a real one from matching.
    FakeUnit unit;
    unit.AddAura(100, 0);
    unit.AddAura(200, 0);

    CHECK(!unit.CanAnyAuraProcFrom(0xFFFFFFFF));

    unit.AddAura(300, 0x00000040);
    CHECK(unit.CanAnyAuraProcFrom(0x00000040));

    unit.RemoveAura(300);
    CHECK(!unit.CanAnyAuraProcFrom(0xFFFFFFFF));
}

TEST(ProcIndex_DeletedHoldersDropOutOfTheAggregate)
{
    // The real enumerator skips holders flagged deleted but not yet erased.
    FakeUnit unit;
    unit.AddAura(100, 0x00000008);
    CHECK(unit.CanAnyAuraProcFrom(0x00000008));

    unit.MarkDeleted(100);
    CHECK(!unit.CanAnyAuraProcFrom(0x00000008));
    CHECK_EQ(unit.Index().GetAggregate(), 0u);
}

TEST(ProcIndex_TopBitIsNotLost)
{
    // PROC_FLAG_* reaches bit 27 in 2.4.3, but the mask is uint32 and a signed
    // slip would break the top of the range rather than the middle.
    FakeUnit unit;
    unit.AddAura(100, 0x80000000);

    CHECK(unit.CanAnyAuraProcFrom(0x80000000));
    CHECK_EQ(unit.Index().GetAggregate(), 0x80000000u);
    CHECK(!unit.CanAnyAuraProcFrom(0x7FFFFFFF));
}

TEST(ProcIndex_RepeatedQueriesRebuildOnlyOnce)
{
    FakeUnit unit;
    unit.AddAura(100, 0x2);

    CHECK(unit.Index().IsDirty());
    unit.CanAnyAuraProcFrom(0x2);
    CHECK(!unit.Index().IsDirty());

    // Querying again must not dirty it; that is the whole point of the cache.
    unit.CanAnyAuraProcFrom(0x4);
    unit.CanAnyAuraProcFrom(0x2);
    CHECK(!unit.Index().IsDirty());
    CHECK_EQ(unit.Index().GetAggregate(), 0x2u);
}

TEST(ProcIndex_SurvivesRandomAddRemoveSequences)
{
    // The property that must hold no matter the order: after any sequence of
    // changes, the aggregate equals the OR of what is actually held. A missing
    // Invalidate() anywhere shows up here as an aggregate that is too small.
    for (uint32 seed = 1; seed <= 64; ++seed)
    {
        Rng rng(seed);
        FakeUnit unit;
        std::vector<uint32> live;

        for (uint32 step = 0; step < 200; ++step)
        {
            const bool add = live.empty() || (rng.Below(100) < 60);

            if (add)
            {
                const uint32 id = 1000 + rng.Below(50);
                const uint32 flags = rng.Next() & 0x0FFFFFFF;
                unit.AddAura(id, flags);
                live.push_back(id);
            }
            else
            {
                const uint32 victim = rng.Below(uint32(live.size()));
                unit.RemoveAura(live[victim]);
                live.erase(live.begin() + victim);
            }

            // Force a rebuild, then compare against the slow computation.
            unit.CanAnyAuraProcFrom(0xFFFFFFFF);

            const uint32 expected = unit.ExpectedAggregate();
            if (unit.Index().GetAggregate() != expected)
            {
                testing::ReportFailure(__FILE__, __LINE__,
                    "seed " + std::to_string(seed) + " step " + std::to_string(step) +
                    ": aggregate " + std::to_string(unit.Index().GetAggregate()) +
                    " != expected " + std::to_string(expected));
                return;
            }
        }
    }
}

TEST(ProcIndex_NoHeldFlagIsEverMissed)
{
    // The one-sided failure stated at the top of this file, checked directly:
    // for every bit any held aura carries, the index must say yes.
    for (uint32 seed = 1; seed <= 32; ++seed)
    {
        Rng rng(seed);
        FakeUnit unit;

        std::vector<uint32> heldFlags;
        for (uint32 i = 0; i < 12; ++i)
        {
            const uint32 flags = uint32(1) << rng.Below(32);
            unit.AddAura(2000 + i, flags);
            heldFlags.push_back(flags);
        }

        for (uint32 flags : heldFlags)
        {
            if (!unit.CanAnyAuraProcFrom(flags))
            {
                testing::ReportFailure(__FILE__, __LINE__,
                    "seed " + std::to_string(seed) + ": held flag " +
                    std::to_string(flags) + " reported as unable to proc");
                return;
            }
        }

        // And a bit nobody holds must still be rejected.
        uint32 unheld = 0;
        for (uint32 flags : heldFlags)
        {
            unheld |= flags;
        }
        unheld = ~unheld;
        if (unheld && unit.CanAnyAuraProcFrom(unheld))
        {
            testing::ReportFailure(__FILE__, __LINE__,
                "seed " + std::to_string(seed) + ": unheld flags reported as able to proc");
            return;
        }
    }
}
