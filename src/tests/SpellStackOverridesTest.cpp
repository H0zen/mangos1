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
 * @file SpellStackOverridesTest.cpp
 * @brief Cases for the recorded spell-stacking exceptions.
 *
 * The rules these rows replace were unreachable from a test: they lived inside
 * a six-hundred-line switch that needed two real Spell.dbc rows to enter. As
 * data matched against three plain fields they can be driven directly, which is
 * the whole reason for moving them.
 *
 * What is pinned here is the matching, not the game facts. That a visual and
 * its spell may stack is a design decision recorded in the table; that a
 * symmetric row fires whichever way round the pair arrives is a property of the
 * matcher, and it is the one a careless edit would break -- the original code
 * spelled both directions out by hand in every single rule.
 */

#include "TestHarness.h"

#include "SpellStackOverrides.h"

namespace
{
    /// A spell that only has an id; icon and visual stay zero.
    StackSpellFacts WithId(uint32 id)
    {
        StackSpellFacts f;
        f.id = id;
        return f;
    }

    /// A spell identified by icon, and optionally by visual.
    StackSpellFacts WithIcon(uint32 icon, uint32 visual = 0)
    {
        StackSpellFacts f;
        f.id = 1;
        f.iconId = icon;
        f.visualId = visual;
        return f;
    }

    /// A spell with an id, an icon, a visual and a family mask.
    StackSpellFacts Spell(uint32 id, uint32 icon, uint32 visual, uint64 mask)
    {
        StackSpellFacts f;
        f.id = id;
        f.iconId = icon;
        f.visualId = visual;
        f.familyMask = mask;
        return f;
    }
}

TEST(SpellStackOverrides_SymmetricIdPairFiresBothWaysRound)
{
    bool noStack = true;

    // Thunderfury and its proc.
    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithId(21992), WithId(27648), noStack));
    CHECK(!noStack);

    noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithId(27648), WithId(21992), noStack));
    CHECK(!noStack);
}

TEST(SpellStackOverrides_UnrecordedPairIsNotFound)
{
    bool noStack = false;

    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithId(21992), WithId(12345), noStack));
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithId(1), WithId(2), noStack));
}

TEST(SpellStackOverrides_TheOutParamIsUntouchedWithoutAMatch)
{
    // The derivation carries on when nothing matched, so a miss must not be
    // able to plant a verdict.
    bool noStack = true;
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithId(1), WithId(2), noStack));
    CHECK(noStack);

    noStack = false;
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithId(1), WithId(2), noStack));
    CHECK(!noStack);
}

TEST(SpellStackOverrides_OneSpellCanPairWithSeveralPartners)
{
    // 32756 is recorded against both the male and the female disguise, so a
    // matcher that stopped at the first row mentioning it would be wrong.
    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithId(32756), WithId(38080), noStack));
    CHECK(!noStack);

    noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithId(32756), WithId(38081), noStack));
    CHECK(!noStack);

    // ...but not against an id it was never paired with.
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithId(32756), WithId(38082), noStack));
}

TEST(SpellStackOverrides_IconRuleNeedsBothSidesToCarryIt)
{
    bool noStack = true;

    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(240), WithIcon(240), noStack));
    CHECK(!noStack);

    // One side alone is not enough.
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(240), WithIcon(241), noStack));
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(241), WithIcon(240), noStack));
}

TEST(SpellStackOverrides_IconAndVisualRuleSeparatesTheVisualFromTheSpell)
{
    bool noStack = true;

    // Soulstone Resurrection and Twisting Nether: same icon, visual 99 against
    // visual 0, either way round.
    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(92, 99), WithIcon(92, 0), noStack));
    CHECK(!noStack);

    noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(92, 0), WithIcon(92, 99), noStack));
    CHECK(!noStack);

    // Same icon but the visuals do not fit the recorded pair.
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(92, 99), WithIcon(92, 99), noStack));
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(92, 0), WithIcon(92, 0), noStack));
    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(92, 7), WithIcon(92, 0), noStack));
}

TEST(SpellStackOverrides_AnIdRuleIsNotMatchedByIconsAlone)
{
    // Every fact defaults to zero, so a matcher that compared the wrong field
    // would make two blank spells match an id row.
    StackSpellFacts blank;
    bool noStack = false;

    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, blank, blank, noStack));
}

TEST(SpellStackOverrides_TableRowsAreWellFormed)
{
    for (size_t i = 0; i < kSpellStackOverrideCount; ++i)
    {
        SpellStackOverride const& row = kSpellStackOverrides[i];

        // Every row carries the comment its rule had; that note is the only
        // record of why the exception exists.
        CHECK(row.note != nullptr);
        CHECK(row.note[0] != '\0');

        if (row.match == SOM_ID_PAIR || row.match == SOM_ID1_ID2)
        {
            // A row pairing a spell with itself would fire on every self-test.
            CHECK(row.a != row.b);
            CHECK(row.a != 0);
            CHECK(row.b != 0);
        }

        if (row.match == SOM_ID1_MASK2)
        {
            // A zero mask intersects nothing, so such a row could never fire.
            CHECK(row.mask != 0);
            CHECK(row.a != 0);
        }

        if (row.match == SOM_ICON_BOTH || row.match == SOM_ICON_BOTH_VISUAL_PAIR)
        {
            // Icon 0 is "no icon", carried by a great many rows.
            CHECK(row.a != 0);
        }
    }
}

TEST(SpellStackOverrides_DirectionalIdRuleDoesNotFireReversed)
{
    // Dragonmaw Illusion is recorded once as GENERIC->DRUID (40216, 42016) and
    // again, mirrored, under the DRUID branch. If this row also fired reversed
    // it would answer for a pairing that is not its own.
    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_DRUID, WithId(40216), WithId(42016), noStack));
    CHECK(!noStack);

    CHECK(!FindStackOverride(SOC_GENERIC_DRUID, WithId(42016), WithId(40216), noStack));
}

TEST(SpellStackOverrides_DirectionalIconAndVisualRuleDoesNotFireReversed)
{
    // Scroll of Protection carries the icon and visual; Defensive Stance is
    // named by id. Swapping the two must not match.
    const StackSpellFacts scroll = Spell(0, 276, 196, 0);
    const StackSpellFacts stance = Spell(71, 0, 0, 0);

    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_WARRIOR, scroll, stance, noStack));
    CHECK(!noStack);

    CHECK(!FindStackOverride(SOC_GENERIC_WARRIOR, stance, scroll, noStack));
}

TEST(SpellStackOverrides_MaskRuleTestsTheSecondSpellsFamilyMask)
{
    // Improved Hamstring is named by id, Hamstring by a family-mask bit.
    const StackSpellFacts improved = Spell(23694, 0, 0, 0);
    const StackSpellFacts hamstring = Spell(0, 0, 0, UI64LIT(0x2));

    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_WARRIOR, improved, hamstring, noStack));
    CHECK(!noStack);

    // A different bit is not the rule's bit.
    const StackSpellFacts other = Spell(0, 0, 0, UI64LIT(0x4));
    CHECK(!FindStackOverride(SOC_GENERIC_WARRIOR, improved, other, noStack));

    // An empty mask never intersects.
    CHECK(!FindStackOverride(SOC_GENERIC_WARRIOR, improved, Spell(0, 0, 0, 0), noStack));

    // Directional: the mask belongs to spell 2.
    CHECK(!FindStackOverride(SOC_GENERIC_WARRIOR, hamstring, improved, noStack));
}

TEST(SpellStackOverrides_MaskRuleMatchesOnAnyOverlappingBit)
{
    // The rule is an intersection test, not equality, so a spell carrying the
    // bit among others still matches.
    const StackSpellFacts improved = Spell(23694, 0, 0, 0);
    const StackSpellFacts many = Spell(0, 0, 0, UI64LIT(0x2) | UI64LIT(0x8000));

    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_WARRIOR, improved, many, noStack));
    CHECK(!noStack);
}

TEST(SpellStackOverrides_ContextSeparatesRowsThatWouldOtherwiseMatch)
{
    // The GENERIC/ANY illusion row is an icon-both rule; asking for it under a
    // branch it does not belong to must not find it.
    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_ANY, WithIcon(1691), WithIcon(1691), noStack));
    CHECK(!noStack);

    CHECK(!FindStackOverride(SOC_GENERIC_GENERIC, WithIcon(1691), WithIcon(1691), noStack));
    CHECK(!FindStackOverride(SOC_GENERIC_MAGE, WithIcon(1691), WithIcon(1691), noStack));
}

TEST(SpellStackOverrides_VisualBearingSideIsTheRecordedOne)
{
    // Sanctity Aura: both icons 502, but only spell 1's visual is tested.
    bool noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_PALADIN, WithIcon(502, 969), WithIcon(502, 0), noStack));
    CHECK(!noStack);

    // The same visual on the other side is a different rule, and not this one.
    CHECK(!FindStackOverride(SOC_GENERIC_PALADIN, WithIcon(502, 0), WithIcon(502, 969), noStack));

    // Seal of Righteousness and Head Crack test spell 2's visual instead.
    noStack = true;
    CHECK(FindStackOverride(SOC_GENERIC_PALADIN, WithIcon(25, 0), WithIcon(25, 7986), noStack));
    CHECK(!noStack);

    CHECK(!FindStackOverride(SOC_GENERIC_PALADIN, WithIcon(25, 7986), WithIcon(25, 0), noStack));
}

TEST(SpellStackOverrides_EveryMigratedRowSaysTheyMayStack)
{
    /*
     * Every branch migrated so far is uniform -- all of its rules answer "these
     * may stack" -- and the migration leans on exactly that: because no row can
     * disagree with another, their relative order cannot change an answer, so
     * they did not have to be proved un-preemptible one by one.
     *
     * The branches still left in code are NOT uniform. WARRIOR/WARRIOR returns
     * true for the stances, WARLOCK/WARLOCK for Seed of Corruption, and
     * PALADIN/PALADIN for two seals, each sitting among rules that return
     * false. When those move here their order becomes load-bearing and this
     * test must be replaced by one that pins it -- not merely relaxed.
     */
    for (size_t i = 0; i < kSpellStackOverrideCount; ++i)
    {
        CHECK(!kSpellStackOverrides[i].noStack);
    }
}
