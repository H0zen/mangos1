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
 * The proc base-points vocabulary.
 *
 * Rows in spell_proc_trigger are data and the loader checks them one by one.
 * What needs proving is the vocabulary those rows are written in: every proc
 * in the game computes its base points through `source * percent / 100`, so
 * an error here is an error in every row at once.
 *
 * Each case below pins one shape the vocabulary has to express.
 */

#include "TestHarness.h"

#include "combat/pure/ProcPoints.h"
#include "combat/pure/ProcTrigger.h"

#include <cstdint>

using namespace Combat;

namespace
{
    PointsInputs Inputs()
    {
        PointsInputs in;
        in.damage             = 1000;
        in.auraAmount         = 15;
        in.auraEffectValue    = 40;
        in.actorMaxHealth     = 8000;
        in.actorMaxMana       = 5000;
        in.actorAttackPower   = 2200;
        in.targetCreateHealth = 12000;
        in.procSpellManaCost  = 300;
        in.weaponDamage       = 450;
        return in;
    }

    PointsFormula Formula(PointsSource source, std::int32_t coeff,
                          PointsScale scale = PointsScale::Literal,
                          std::int32_t divisor = 1)
    {
        PointsFormula f;
        f.source  = source;
        f.scale   = scale;
        f.coeff   = coeff;
        f.divisor = divisor;
        return f;
    }
}

// ---------------------------------------------------------------------------
// The shape itself.
// ---------------------------------------------------------------------------

TEST(ProcPointsUndefinedFormulaYieldsNothing)
{
    // No row means the triggered spell keeps its own base points.
    PointsFormula f;
    CHECK(!f.Defined());
    CHECK_EQ(EvaluatePoints(f, Inputs()), 0);
}

TEST(ProcPointsHundredPercentIsIdentity)
{
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 100), Inputs()),
             1000);
}

TEST(ProcPointsDivisorSpreadsOverTicks)
{
    // A damage-over-time proc spreads its total across ticks.
    const PointsFormula f =
        Formula(PointsSource::Damage, 0, PointsScale::AuraAmount, 3);

    CHECK_EQ(EvaluatePoints(f, Inputs()), 1000 * 15 / 100 / 3);
}

TEST(ProcPointsZeroDivisorIsOne)
{
    PointsFormula f = Formula(PointsSource::Damage, 100);
    f.divisor = 0;

    CHECK_EQ(EvaluatePoints(f, Inputs()), 1000);
}

TEST(ProcPointsSurvivesALargeProduct)
{
    // Attack power against a 300% coefficient overflows a 32-bit product on a
    // geared character, so the multiply is widened.
    PointsInputs in;
    in.actorAttackPower = 3000000;

    const PointsFormula f = Formula(PointsSource::ActorAttackPower, 300);

    CHECK_EQ(EvaluatePoints(f, in), 9000000);
}

// ---------------------------------------------------------------------------
// The shapes the vocabulary has to express, one test each.
// ---------------------------------------------------------------------------

TEST(ProcPointsReproducesDamageCoefficients)
{
    const PointsInputs in = Inputs();

    // Ignite rank 1: 4% of the damage that procced it.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 4), in),
             std::int32_t(0.04f * 1000));

    // Ignite rank 5: 20% of it.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 20), in),
             std::int32_t(0.20f * 1000));

    // A plain share of the damage.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 15), in),
             1000 * 15 / 100);

    // A multiple of it: coefficients above 100 are ordinary.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 300), in),
             3 * 1000);

    // And a fractional multiple.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 250), in),
             std::int32_t(1000 * 2.5f));
}

TEST(ProcPointsReproducesWeaponCoefficients)
{
    const PointsInputs in = Inputs();

    // Deep Wounds rank 1: a fifth of the swing.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::WeaponDamage, 20), in),
             std::int32_t(450 * 0.2f));

    // Rank 3 scales the same shape.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::WeaponDamage, 60), in),
             std::int32_t(450 * 0.6f));
}

TEST(ProcPointsReproducesAuraAmountAsThePercentage)
{
    const PointsInputs in = Inputs();

    // The aura carries the percentage; the source is the damage.
    CHECK_EQ(EvaluatePoints(
                 Formula(PointsSource::Damage, 0, PointsScale::AuraAmount), in),
             15 * 1000 / 100);

    // Same percentage against a health pool.
    CHECK_EQ(EvaluatePoints(
                 Formula(PointsSource::ActorMaxHealth, 0,
                         PointsScale::AuraAmount), in),
             15 * 8000 / 100);

    // ...against a mana pool.
    CHECK_EQ(EvaluatePoints(
                 Formula(PointsSource::ActorMaxMana, 0,
                         PointsScale::AuraAmount), in),
             15 * 5000 / 100);

    // ...against what the spell that procced cost to cast.
    CHECK_EQ(EvaluatePoints(
                 Formula(PointsSource::ProcSpellManaCost, 0,
                         PointsScale::AuraAmount), in),
             300 * 15 / 100);

    // ...and against attack power.
    CHECK_EQ(EvaluatePoints(
                 Formula(PointsSource::ActorAttackPower, 0,
                         PointsScale::AuraAmount), in),
             2200 * 15 / 100);
}

TEST(ProcPointsReproducesAuraAmountAsTheWholeValue)
{
    // The aura's amount used whole, rather than as a percentage.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::AuraAmount, 100), Inputs()),
             15);
}

TEST(ProcPointsReproducesTheEffectValueScale)
{
    const PointsInputs in = Inputs();

    // The percentage can also come off a second effect of the aura spell.
    CHECK_EQ(EvaluatePoints(
                 Formula(PointsSource::TargetCreateHealth, 0,
                         PointsScale::AuraEffectValue), in),
             12000 * 40 / 100);
}

TEST(ProcPointsReproducesHalfOfMaxHealth)
{
    // Half a health pool is just a 50% coefficient.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::ActorMaxHealth, 50),
                            Inputs()),
             8000 / 2);
}

TEST(ProcPointsReproducesFlatManaCostShares)
{
    const PointsInputs in = Inputs();

    // Fixed shares of the cast cost.
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::ProcSpellManaCost, 30), in),
             300 * 30 / 100);
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::ProcSpellManaCost, 35), in),
             300 * 35 / 100);
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::ProcSpellManaCost, 40), in),
             300 * 40 / 100);
}

TEST(ProcPointsCoefficientIsExactPercent)
{
    // 12% of 1000 is 120, not 119. Coefficients are integer percent and the
    // only truncation is the final divide, so a coefficient means exactly
    // what it says at every magnitude.
    PointsInputs in;
    in.damage = 1000;

    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 12), in), 120);
    CHECK_EQ(EvaluatePoints(Formula(PointsSource::Damage, 33), in), 330);
}

// ---------------------------------------------------------------------------
// Names, which the SQL loader parses.
// ---------------------------------------------------------------------------

TEST(ProcPointsNamesRoundTrip)
{
    for (std::uint8_t i = 0;
         i < static_cast<std::uint8_t>(PointsSource::Count); ++i)
    {
        const PointsSource source = static_cast<PointsSource>(i);

        PointsSource parsed = PointsSource::Count;
        CHECK(ParsePointsSource(NameOf(source), parsed));
        CHECK(parsed == source);
    }

    for (std::uint8_t i = 0;
         i < static_cast<std::uint8_t>(PointsScale::Count); ++i)
    {
        const PointsScale scale = static_cast<PointsScale>(i);

        PointsScale parsed = PointsScale::Count;
        CHECK(ParsePointsScale(NameOf(scale), parsed));
        CHECK(parsed == scale);
    }

    for (std::uint8_t i = 0;
         i < static_cast<std::uint8_t>(ProcTarget::Count); ++i)
    {
        const ProcTarget target = static_cast<ProcTarget>(i);

        ProcTarget parsed = ProcTarget::Count;
        CHECK(ParseProcTarget(NameOf(target), parsed));
        CHECK(parsed == target);
    }
}

TEST(ProcPointsRejectsAnUnknownName)
{
    PointsSource source = PointsSource::Damage;
    CHECK(!ParsePointsSource("spell_icon", source));
    CHECK(!ParsePointsSource(nullptr, source));

    PointsScale scale = PointsScale::Literal;
    CHECK(!ParsePointsScale("whatever", scale));

    ProcTarget target = ProcTarget::Victim;
    CHECK(!ParseProcTarget("everyone", target));
}

TEST(ProcTriggerDefaultIsUnknownAndGeneric)
{
    ProcTrigger row;

    CHECK(!row.known);
    CHECK(!row.IsNamed());
    CHECK_EQ(row.triggerSpell, 0u);
    CHECK(row.target == ProcTarget::Victim);
    CHECK(!row.points.Defined());
}
