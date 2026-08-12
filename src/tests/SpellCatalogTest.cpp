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
 * @file SpellCatalogTest.cpp
 * @brief Cases for the boot-time spell derivation.
 *
 * These run against synthetic Spell.dbc rows, not against client data, because
 * the point is to pin the derivation rules rather than to inventory 2.4.3. Two
 * of them exist specifically to catch the mistakes this refactor could have
 * made: ProcFlagsAreAnOverrideNotAUnion, because or-ing the two sources would
 * let a spell proc on flags the SQL row was written to remove; and
 * PeriodicTriggerJudgesTheTriggeredSpell, because that is the one derivation
 * that consults another spell and so the one that pass ordering could break.
 *
 * The wider check -- that every real spell classifies the same as it did before
 * -- cannot live here: it needs the client DBC, and the test binary
 * deliberately links no part of the server. That one belongs at boot.
 */

#include "TestHarness.h"

#include "SpellCatalog.h"

#include <cstring>
#include <memory>
#include <vector>

namespace
{
    /**
     * @brief A zeroed, writable Spell.dbc row.
     *
     * SpellEntry declares a private copy constructor, which suppresses the
     * implicit default one, so it cannot be declared or aggregate-initialised.
     * Owning the storage and handing back a reference is what the DBC loader
     * itself does with a raw record buffer.
     */
    class FakeSpell
    {
        public:
            explicit FakeSpell(uint32 id) : m_storage(sizeof(SpellEntry), 0)
            {
                Get().ID = id;
            }

            SpellEntry& Get()
            {
                return *reinterpret_cast<SpellEntry*>(m_storage.data());
            }

            SpellEntry const* Ptr()
            {
                return reinterpret_cast<SpellEntry const*>(m_storage.data());
            }

        private:
            std::vector<unsigned char> m_storage;
    };

    /// Sources with nothing in them: no elixirs, no overrides.
    SpellCatalogSources NoSources()
    {
        return SpellCatalogSources();
    }
}

TEST(SpellCatalog_EmptyIdYieldsAZeroedEntry)
{
    SpellCatalog catalog;
    FakeSpell known(42);
    catalog.Build({known.Ptr()}, NoSources());

    SpellInfo const& missing = catalog.Get(999);
    CHECK(missing.dbc == nullptr);
    CHECK_EQ(missing.procFlags, 0u);
    CHECK(catalog.Find(999) == nullptr);

    // Well past the end of the id table, which is sized to the highest id seen.
    CHECK(catalog.Get(0x7FFFFFFF).dbc == nullptr);

    CHECK(catalog.Find(42) != nullptr);
    CHECK_EQ(catalog.GetSpellCount(), 1u);
    CHECK(catalog.IsBuilt());
}

TEST(SpellCatalog_EffectAndAuraMasksTrackTheRow)
{
    SpellCatalog catalog;
    FakeSpell spell(100);
    SpellEntry& e = spell.Get();

    e.Effect[EFFECT_INDEX_0] = SPELL_EFFECT_SCHOOL_DAMAGE;
    e.Effect[EFFECT_INDEX_1] = SPELL_EFFECT_APPLY_AURA;
    e.EffectAura[EFFECT_INDEX_1] = SPELL_AURA_PERIODIC_DAMAGE;
    e.Effect[EFFECT_INDEX_2] = SPELL_EFFECT_NONE;

    catalog.Build({spell.Ptr()}, NoSources());
    SpellInfo const& info = catalog.Get(100);

    REQUIRE(info.dbc != nullptr);
    CHECK_EQ(uint32(info.effectMask), 0x3u);
    CHECK_EQ(uint32(info.auraEffectMask), 0x2u);

    CHECK(info.HasAuraType(SPELL_AURA_PERIODIC_DAMAGE));
    CHECK(!info.HasAuraType(SPELL_AURA_MOD_STUN));

    // Out-of-range aura ids must answer false rather than index off the bitset.
    CHECK(!info.HasAuraType(AuraType(TOTAL_AURAS)));
    CHECK(!info.HasAuraType(AuraType(TOTAL_AURAS + 5000)));
}

TEST(SpellCatalog_AuraBitsetSpansEveryWord)
{
    // TOTAL_AURAS is 262, so the bitset is five 64-bit words and the last one is
    // mostly padding. A shift or word-index slip shows up at the boundaries.
    const AuraType probes[] =
    {
        AuraType(0), AuraType(63), AuraType(64), AuraType(127),
        AuraType(128), AuraType(191), AuraType(192), AuraType(255),
        AuraType(TOTAL_AURAS - 1)
    };

    for (AuraType probe : probes)
    {
        SpellCatalog catalog;
        FakeSpell spell(200);
        spell.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_APPLY_AURA;
        spell.Get().EffectAura[EFFECT_INDEX_0] = uint32(probe);

        catalog.Build({spell.Ptr()}, NoSources());
        SpellInfo const& info = catalog.Get(200);

        if (probe == AuraType(0))
        {
            // Aura id 0 is "no aura"; the row carries it but no bit is claimed.
            CHECK(!info.HasAuraType(probe));
            continue;
        }

        CHECK(info.HasAuraType(probe));

        // Exactly one bit, and it is in the word the id says it should be.
        uint32 set = 0;
        for (uint32 w = 0; w < SPELL_AURA_TYPE_WORDS; ++w)
        {
            for (uint32 b = 0; b < 64; ++b)
            {
                if (info.auraTypes[w] & (uint64(1) << b))
                {
                    ++set;
                    CHECK_EQ(w * 64 + b, uint32(probe));
                }
            }
        }
        CHECK_EQ(set, 1u);
    }
}

TEST(SpellCatalog_ProcFlagsAreAnOverrideNotAUnion)
{
    // IsTriggeredAtSpellProcEvent takes spell_proc_event.procFlags whenever it is
    // nonzero and only falls back to ProcTypeMask otherwise. Or-ing the two would
    // silently re-enable flags an override was written to take away.
    static SpellProcEventEntry override_;
    std::memset(&override_, 0, sizeof(override_));
    override_.procFlags = 0x00000004;

    SpellCatalogSources sources = NoSources();
    sources.GetProcEvent = [](void const*, uint32 spellId) -> SpellProcEventEntry const*
    {
        return spellId == 300 ? &override_ : nullptr;
    };

    SpellCatalog catalog;
    FakeSpell overridden(300);
    overridden.Get().ProcTypeMask = 0x00000011;
    FakeSpell plain(301);
    plain.Get().ProcTypeMask = 0x00000011;

    catalog.Build({overridden.Ptr(), plain.Ptr()}, sources);

    // The override replaces the DBC value outright.
    CHECK_EQ(catalog.Get(300).procFlags, 0x00000004u);
    CHECK(catalog.Get(300).procEvent == &override_);

    // With no override the DBC value stands.
    CHECK_EQ(catalog.Get(301).procFlags, 0x00000011u);
    CHECK(catalog.Get(301).procEvent == nullptr);
}

TEST(SpellCatalog_ZeroProcFlagsInOverrideFallsBackToTheDbc)
{
    // A spell_proc_event row that sets only a cooldown or a ppmRate leaves
    // procFlags at zero, and must not blank the DBC mask.
    static SpellProcEventEntry partial;
    std::memset(&partial, 0, sizeof(partial));
    partial.procFlags = 0;
    partial.cooldown = 45;

    SpellCatalogSources sources = NoSources();
    sources.GetProcEvent = [](void const*, uint32) -> SpellProcEventEntry const*
    {
        return &partial;
    };

    SpellCatalog catalog;
    FakeSpell spell(310);
    spell.Get().ProcTypeMask = 0x00040000;
    catalog.Build({spell.Ptr()}, sources);

    CHECK_EQ(catalog.Get(310).procFlags, 0x00040000u);
    CHECK(catalog.Get(310).procEvent == &partial);
}

TEST(SpellCatalog_PositiveRequiresEveryEffectPositive)
{
    SpellCatalog catalog;

    // A plain heal on a friendly target.
    FakeSpell heal(400);
    heal.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_HEAL;
    heal.Get().ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;

    // The same heal, with a root stapled onto the second effect.
    FakeSpell mixed(401);
    mixed.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_HEAL;
    mixed.Get().ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;
    mixed.Get().Effect[EFFECT_INDEX_1] = SPELL_EFFECT_APPLY_AURA;
    mixed.Get().EffectAura[EFFECT_INDEX_1] = SPELL_AURA_MOD_ROOT;
    mixed.Get().ImplicitTargetA[EFFECT_INDEX_1] = TARGET_SINGLE_FRIEND;

    catalog.Build({heal.Ptr(), mixed.Ptr()}, NoSources());

    CHECK(catalog.Get(400).positive);
    CHECK(catalog.Get(400).IsEffectPositive(EFFECT_INDEX_0));

    CHECK(!catalog.Get(401).positive);
    CHECK(catalog.Get(401).IsEffectPositive(EFFECT_INDEX_0));
    CHECK(!catalog.Get(401).IsEffectPositive(EFFECT_INDEX_1));

    // An empty effect is neither positive nor counted against the spell.
    CHECK(!catalog.Get(400).IsEffectPositive(EFFECT_INDEX_2));
}

TEST(SpellCatalog_PeriodicTriggerJudgesTheTriggeredSpell)
{
    // The one derivation that consults another spell: a periodic trigger aura
    // whose triggered spell is negative on a positive target makes the parent
    // negative. Pass two must therefore see every row already registered --
    // this fails outright if Build() derives in a single pass.
    SpellCatalog catalog;

    FakeSpell parent(500);
    parent.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_APPLY_AURA;
    parent.Get().EffectAura[EFFECT_INDEX_0] = SPELL_AURA_PERIODIC_TRIGGER_SPELL;
    parent.Get().EffectTriggerSpell[EFFECT_INDEX_0] = 501;
    parent.Get().ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;

    FakeSpell triggered(501);
    triggered.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_APPLY_AURA;
    triggered.Get().EffectAura[EFFECT_INDEX_0] = SPELL_AURA_MOD_SILENCE;
    triggered.Get().ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;

    // Parent listed first, so a one-pass build would not yet know spell 501.
    catalog.Build({parent.Ptr(), triggered.Ptr()}, NoSources());

    CHECK(!catalog.Get(501).positive);
    CHECK(!catalog.Get(500).positive);

    // And with a harmless triggered spell the parent stays positive.
    SpellCatalog benign;
    FakeSpell parent2(510);
    parent2.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_APPLY_AURA;
    parent2.Get().EffectAura[EFFECT_INDEX_0] = SPELL_AURA_PERIODIC_TRIGGER_SPELL;
    parent2.Get().EffectTriggerSpell[EFFECT_INDEX_0] = 511;
    parent2.Get().ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;

    FakeSpell triggered2(511);
    triggered2.Get().Effect[EFFECT_INDEX_0] = SPELL_EFFECT_HEAL;
    triggered2.Get().ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;

    benign.Build({parent2.Ptr(), triggered2.Ptr()}, NoSources());
    CHECK(benign.Get(510).positive);
}

TEST(SpellCatalog_ElixirMaskDecidesTheExclusionClass)
{
    // Elixir classification is the one part of GetSpellSpecific that comes from
    // SQL rather than the DBC, so it arrives as a mask instead of a lookup.
    struct Expectation
    {
        uint32 id;
        uint32 mask;
        SpellSpecific expected;
    };

    static const Expectation cases[] =
    {
        {600, ELIXIR_BATTLE_MASK,   SPELL_BATTLE_ELIXIR},
        {601, ELIXIR_GUARDIAN_MASK, SPELL_GUARDIAN_ELIXIR},
        {602, ELIXIR_FLASK_MASK,    SPELL_FLASK_ELIXIR},
        {603, ELIXIR_WELL_FED,      SPELL_WELL_FED},
        {604, 0,                    SPELL_NORMAL},
    };

    for (Expectation const& c : cases)
    {
        FakeSpell spell(c.id);
        spell.Get().SpellClassSet = SPELLFAMILY_POTION;

        static uint32 activeMask;
        activeMask = c.mask;

        SpellCatalogSources sources = NoSources();
        sources.GetElixirMask = [](void const*, uint32) -> uint32
        {
            return activeMask;
        };

        SpellCatalog catalog;
        catalog.Build({spell.Ptr()}, sources);
        CHECK_EQ(int(catalog.Get(c.id).specific), int(c.expected));
    }
}

TEST(SpellCatalog_PassiveComesStraightFromTheAttribute)
{
    SpellCatalog catalog;

    FakeSpell passive(700);
    passive.Get().Attributes = SPELL_ATTR_PASSIVE;
    FakeSpell active(701);

    catalog.Build({passive.Ptr(), active.Ptr()}, NoSources());

    CHECK(catalog.Get(700).passive);
    CHECK(!catalog.Get(701).passive);
}

TEST(SpellCatalog_RebuildReplacesEverything)
{
    // .reload spell_proc_event rebuilds in place; stale entries must not survive.
    SpellCatalog catalog;

    FakeSpell first(800);
    first.Get().ProcTypeMask = 0x1;
    catalog.Build({first.Ptr()}, NoSources());
    CHECK_EQ(catalog.GetSpellCount(), 1u);
    CHECK(catalog.Find(800) != nullptr);

    FakeSpell second(900);
    second.Get().ProcTypeMask = 0x2;
    catalog.Build({second.Ptr()}, NoSources());

    CHECK_EQ(catalog.GetSpellCount(), 1u);
    CHECK(catalog.Find(800) == nullptr);
    REQUIRE(catalog.Find(900) != nullptr);
    CHECK_EQ(catalog.Get(900).procFlags, 0x2u);
}

TEST(SpellCatalog_NullRowsAndDuplicateIdsDoNotCorruptTheIndex)
{
    SpellCatalog catalog;

    FakeSpell a(1000);
    a.Get().ProcTypeMask = 0xAA;
    FakeSpell duplicate(1000);
    duplicate.Get().ProcTypeMask = 0xBB;
    FakeSpell b(1001);

    catalog.Build({nullptr, a.Ptr(), nullptr, duplicate.Ptr(), b.Ptr()}, NoSources());

    // The second row for id 1000 is dropped, not written over the first.
    CHECK_EQ(catalog.GetSpellCount(), 2u);
    REQUIRE(catalog.Find(1000) != nullptr);
    CHECK_EQ(catalog.Get(1000).procFlags, 0xAAu);
    CHECK(catalog.Get(1000).dbc == a.Ptr());
    CHECK(catalog.Find(1001) != nullptr);
}

TEST(SpellCatalog_DeriveHelpersMatchTheCatalogFields)
{
    // The free Derive* functions are what SpellMgr falls back to before Build()
    // has run. If they and the catalog ever disagree, boot-time and run-time
    // answers differ -- which is exactly the bug this phase must not introduce.
    FakeSpell spell(1100);
    SpellEntry& e = spell.Get();
    e.Attributes = SPELL_ATTR_PASSIVE;
    e.Effect[EFFECT_INDEX_0] = SPELL_EFFECT_APPLY_AURA;
    e.EffectAura[EFFECT_INDEX_0] = SPELL_AURA_MOD_ROOT;
    e.ImplicitTargetA[EFFECT_INDEX_0] = TARGET_SINGLE_FRIEND;

    SpellCatalog catalog;
    catalog.Build({spell.Ptr()}, NoSources());
    SpellInfo const& info = catalog.Get(1100);

    CHECK_EQ(int(info.passive), int(DeriveIsPassiveSpell(spell.Ptr())));
    CHECK_EQ(int(info.IsEffectPositive(EFFECT_INDEX_0)),
             int(DeriveIsPositiveEffect(spell.Ptr(), EFFECT_INDEX_0, nullptr, nullptr)));
    CHECK_EQ(int(info.specific), int(DeriveSpellSpecific(spell.Ptr(), 0)));
}
