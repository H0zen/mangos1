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
 * The first combat tests this core has ever had.
 *
 * Two jobs. Most of these pin what the rules do so that a change to them is
 * visible; a few pin numbers that are known to be WRONG for 2.4.3, and those
 * are named ...IsStillVanilla or ...IsStillUnsourced so that the stage which
 * corrects them turns a test red by name rather than by surprise.
 *
 * The reference matchup throughout is the one that matters: a level 70 player
 * with capped weapon skill swinging at a +3 raid boss.
 */

#include "TestHarness.h"

#include "combat/pure/CombatConstants.h"
#include "combat/pure/HitTable.h"
#include "combat/pure/Matchup.h"
#include "combat/pure/Profile.h"
#include "combat/pure/Rng.h"
#include "combat/pure/Strike.h"
#include "combat/pure/StrikeResolver.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace Combat;

namespace
{
    /// An Rng that hands back exactly what the test wrote down. The resolver
    /// takes its randomness as an argument for precisely this reason.
    class ScriptedRng : public Rng
    {
        public:
            ScriptedRng(Hundredths roll, std::uint32_t damage, float unit)
                : m_roll(roll), m_damage(damage), m_unit(unit)
            {
            }

            Hundredths Roll10000() override
            {
                return m_roll;
            }

            std::uint32_t RollRange(std::uint32_t low,
                                    std::uint32_t high) override
            {
                if (m_damage < low)
                {
                    return low;
                }
                if (m_damage > high)
                {
                    return high;
                }
                return m_damage;
            }

            float RollUnit() override
            {
                return m_unit;
            }

        private:
            Hundredths    m_roll;
            std::uint32_t m_damage;
            float         m_unit;
    };

    /// Level 70, weapon skill at the 350 ceiling, 20% crit.
    Profile Warrior()
    {
        Profile p;
        p.version          = 1;
        p.level            = 70;
        p.kind             = Kind::Player;
        p.maxSkillForLevel = 350;
        p.defenseSkill     = 350;

        for (std::size_t h = 0; h < HAND_COUNT; ++h)
        {
            p.weaponSkill[h] = 350;
            p.critChance[h]  = 2000;
            p.weapon[h]      = DamageRange{100, 100};
        }

        p.caps.mayGlance = true;
        p.caps.canDodge  = true;
        p.caps.canParry  = true;
        return p;
    }

    /// A +3 boss: level 73, defence 365, no shield but flagged to block, which
    /// is what the old core gave every creature in the game.
    Profile Boss()
    {
        Profile p;
        p.version          = 1;
        p.level            = 73;
        p.kind             = Kind::Creature;
        p.maxSkillForLevel = 365;
        p.defenseSkill     = 365;

        for (std::size_t h = 0; h < HAND_COUNT; ++h)
        {
            p.weaponSkill[h] = 365;
            p.critChance[h]  = Constants::CREATURE_CRIT_BASE;
            p.weapon[h]      = DamageRange{500, 500};
        }

        p.dodgeChance = Constants::CREATURE_DODGE_BASE;
        p.parryChance = Constants::CREATURE_PARRY_BASE;
        p.blockChance = Constants::CREATURE_BLOCK_BASE;
        p.blockValue  = 100;

        p.caps.canDodge = true;
        p.caps.canParry = true;
        p.caps.canBlock = true;
        p.caps.mayCrush = true;
        return p;
    }

    Matchup PlayerVersusBoss(Situation const& situation = Situation())
    {
        return Matchup::Build(Warrior(), Boss(), Hand::Main, situation);
    }

    bool Near(float value, float expected, float tolerance)
    {
        return std::fabs(value - expected) <= tolerance;
    }
}

// ---------------------------------------------------------------------------
// The table as a whole
// ---------------------------------------------------------------------------

TEST(CombatTableIsExhaustive)
{
    const HitTable table = HitTable::OneRoll(PlayerVersusBoss());

    // Every roll in range lands somewhere: the bands tile [0, 10000) with no
    // gap and no overlap. This is the property the old cascade of ifs could
    // not state, because its bounds were recomputed inside the branches.
    Hundredths total = 0;
    for (std::size_t i = 0; i < OUTCOME_COUNT; ++i)
    {
        const Outcome outcome = static_cast<Outcome>(i);
        const Hundredths band = table.Band(outcome);
        CHECK(band >= 0);
        total += band;
    }
    CHECK_EQ(total, HUNDRED_PERCENT);
    CHECK_EQ(table.Bound(Outcome::Normal), HUNDRED_PERCENT);
}

TEST(CombatTableResolvesOnEveryBoundary)
{
    const HitTable table = HitTable::OneRoll(PlayerVersusBoss());

    // For each non-empty band, the first roll inside it and the last roll
    // below it must land on different sides. Off-by-one in a cumulative table
    // is invisible in play and fatal to balance.
    for (std::size_t i = 0; i < OUTCOME_COUNT; ++i)
    {
        const Outcome outcome = static_cast<Outcome>(i);
        if (table.Band(outcome) <= 0)
        {
            continue;
        }

        const Hundredths upper = table.Bound(outcome);
        const Hundredths first = upper - table.Band(outcome);

        CHECK(table.Resolve(first) == outcome);
        CHECK(table.Resolve(upper - 1) == outcome);
        if (upper < HUNDRED_PERCENT)
        {
            CHECK(table.Resolve(upper) != outcome);
        }
    }
}

TEST(CombatTableSaturatesWhenOverSubscribed)
{
    Profile boss = Boss();
    boss.dodgeChance = 6000;
    boss.parryChance = 6000;

    const Matchup m =
        Matchup::Build(Warrior(), boss, Hand::Main, Situation());
    const HitTable table = HitTable::OneRoll(m);

    // Enough avoidance and there is no room left for a normal hit. The table
    // must clamp rather than let a later bound wrap past the roll.
    CHECK_EQ(table.Bound(Outcome::Normal), HUNDRED_PERCENT);
    CHECK_EQ(table.Band(Outcome::Normal), 0);
    CHECK(table.Resolve(HUNDRED_PERCENT - 1) != Outcome::Normal);
}

// ---------------------------------------------------------------------------
// The individual chances
// ---------------------------------------------------------------------------

TEST(CombatMissAgainstBossIsNinePercent)
{
    // 5% base, plus 0.4% per point of skill deficit past ten, less the 2%
    // step: 500 - ((-15 + 10) * 40 - 200) = 900.
    CHECK_EQ(PlayerVersusBoss().miss, 900);
}

TEST(CombatDualWieldAddsNineteenToWhiteOnly)
{
    Profile warrior = Warrior();
    warrior.caps.dualWielding = true;

    const Matchup white =
        Matchup::Build(warrior, Boss(), Hand::Main, Situation(), false);
    const Matchup special =
        Matchup::Build(warrior, Boss(), Hand::Main, Situation(), true);

    CHECK_EQ(white.miss, 900 + Constants::MISS_DUAL_WIELD);

    // A special does not carry it. The old core tried to work this out by
    // scanning the current spell slots for anything with a physical school,
    // which meant an auto shot in flight cancelled the penalty on an ordinary
    // white swing.
    CHECK_EQ(special.miss, 900);
}

TEST(CombatSkillDeficitRaisesAvoidance)
{
    const Matchup m = PlayerVersusBoss();

    // skillBonus = 4 * (350 - 365) = -60, subtracted, so the boss avoids more.
    CHECK_EQ(m.dodge, Constants::CREATURE_DODGE_BASE + 60);
    CHECK_EQ(m.parry, Constants::CREATURE_PARRY_BASE + 60);
    CHECK_EQ(m.block, Constants::CREATURE_BLOCK_BASE + 60);
}

TEST(CombatExpertiseEatsDodgeAndParry)
{
    Profile warrior = Warrior();
    for (std::size_t h = 0; h < HAND_COUNT; ++h)
    {
        warrior.expertiseReduction[h] = 200;
    }

    const Matchup m =
        Matchup::Build(warrior, Boss(), Hand::Main, Situation());

    CHECK_EQ(m.dodge, Constants::CREATURE_DODGE_BASE + 60 - 200);
    CHECK_EQ(m.parry, Constants::CREATURE_PARRY_BASE + 60 - 200);

    // Expertise does not touch block. It never did, and the block band must
    // not quietly inherit the reduction.
    CHECK_EQ(m.block, Constants::CREATURE_BLOCK_BASE + 60);
}

TEST(CombatFromBehindRemovesParryAndBlockButNotCreatureDodge)
{
    Situation behind;
    behind.fromBehind = true;

    const Matchup m = PlayerVersusBoss(behind);

    CHECK_EQ(m.parry, 0);
    CHECK_EQ(m.block, 0);

    // Only a player loses its dodge to an attack from behind. A creature
    // dodges you from any angle.
    CHECK(m.dodge > 0);
}

TEST(CombatPlayerLosesDodgeFromBehind)
{
    Profile victim = Warrior();
    victim.dodgeChance = 1500;
    victim.caps.canDodge = true;

    Situation behind;
    behind.fromBehind = true;

    const Matchup m =
        Matchup::Build(Boss(), victim, Hand::Main, behind);

    CHECK_EQ(m.dodge, 0);
}

TEST(CombatCapsDecideBlockAndParryNotCreatureType)
{
    // The audit's finding six: every creature blocked at 5% with no shield,
    // and only humanoids parried. Both questions are a capability bit now, so
    // a boss without a shield simply has no block band.
    Profile boss = Boss();
    boss.caps.canBlock = false;
    boss.caps.canParry = false;

    const Matchup m =
        Matchup::Build(Warrior(), boss, Hand::Main, Situation());

    CHECK_EQ(m.block, 0);
    CHECK_EQ(m.parry, 0);
    CHECK(m.dodge > 0);
}

TEST(CombatCrushOpensAtFifteenSkillPoints)
{
    // The boss swinging at the player: 365 - 350 = 15 points, so 15%.
    const Matchup m =
        Matchup::Build(Boss(), Warrior(), Hand::Main, Situation());

    CHECK_EQ(m.crush, 1500);

    // One more point of deficit is two more percent.
    Profile weaker = Warrior();
    weaker.defenseSkill = 349;
    const Matchup deeper =
        Matchup::Build(Boss(), weaker, Hand::Main, Situation());
    CHECK_EQ(deeper.crush, 1700);
}

TEST(CombatPlayersNeverCrush)
{
    CHECK_EQ(PlayerVersusBoss().crush, 0);
}

TEST(CombatSpecialsNeitherGlanceNorCrush)
{
    const Matchup special =
        Matchup::Build(Warrior(), Boss(), Hand::Main, Situation(), true);
    CHECK_EQ(special.glance, 0);

    const Matchup bossSpecial =
        Matchup::Build(Boss(), Warrior(), Hand::Main, Situation(), true);
    CHECK_EQ(bossSpecial.crush, 0);
}

TEST(CombatTwoRollTableCarriesNoBlockOrCrit)
{
    const Matchup m =
        Matchup::Build(Warrior(), Boss(), Hand::Main, Situation(), true);
    const HitTable table = HitTable::TwoRoll(m);

    // A special rolls its crit and its block separately. The absence of those
    // bands here is why "blocked critical" cannot be an outcome of a one-roll
    // table -- the state the old core had an enumerator for and no way to
    // reach.
    CHECK_EQ(table.Band(Outcome::Block), 0);
    CHECK_EQ(table.Band(Outcome::Crit), 0);
    CHECK_EQ(table.Band(Outcome::Glancing), 0);
    CHECK_EQ(table.Band(Outcome::Crushing), 0);

    CHECK(table.Band(Outcome::Miss) > 0);
    CHECK(table.Band(Outcome::Dodge) > 0);
    CHECK(table.Band(Outcome::Parry) > 0);
}

// ---------------------------------------------------------------------------
// Short circuits
// ---------------------------------------------------------------------------

TEST(CombatEvadeTakesTheWholeTable)
{
    Situation evading;
    evading.victimEvading = true;

    const HitTable table = HitTable::OneRoll(PlayerVersusBoss(evading));

    CHECK_EQ(table.Band(Outcome::Evade), HUNDRED_PERCENT);
    CHECK(table.Resolve(0) == Outcome::Evade);
    CHECK(table.Resolve(HUNDRED_PERCENT - 1) == Outcome::Evade);
}

TEST(CombatSittingPlayerTakesCritButCanStillBeMissed)
{
    Profile victim = Warrior();
    victim.dodgeChance = 2000;
    victim.parryChance = 2000;

    Situation sitting;
    sitting.victimSitting = true;

    const Matchup m =
        Matchup::Build(Boss(), victim, Hand::Main, sitting);
    CHECK(m.sittingCrit);

    const HitTable table = HitTable::OneRoll(m);

    CHECK_EQ(table.Band(Outcome::Dodge), 0);
    CHECK_EQ(table.Band(Outcome::Parry), 0);
    CHECK_EQ(table.Band(Outcome::Normal), 0);

    CHECK(table.Resolve(0) == Outcome::Miss);
    CHECK(table.Resolve(HUNDRED_PERCENT - 1) == Outcome::Crit);
}

// ---------------------------------------------------------------------------
// Resolving damage
// ---------------------------------------------------------------------------

TEST(CombatArmourNeverEatsMoreThanThreeQuarters)
{
    // Level 70, so levelMod is 70 + 4.5 * 11 = 119.5 and the denominator floor
    // is 400 + 85 * 119.5. Enough armour and the cap holds.
    CHECK(Near(StrikeResolver::ArmourSurvival(1000000, 70), 0.25f, 0.001f));
    CHECK(Near(StrikeResolver::ArmourSurvival(0, 70), 1.0f, 0.0001f));

    // Monotonic in armour, and never outside the cap.
    float previous = 1.0f;
    for (std::uint32_t armor = 0; armor < 60000; armor += 1000)
    {
        const float survival = StrikeResolver::ArmourSurvival(armor, 70);
        CHECK(survival <= previous + 0.0001f);
        CHECK(survival >= 0.25f - 0.0001f);
        previous = survival;
    }
}

TEST(CombatNonPhysicalMeleeIsResistedNotArmoured)
{
    // A creature whose melee lands as fire. The old path armoured every
    // school, so this swing was mitigated twice over.
    Profile boss = Boss();
    boss.meleeSchoolMask = 0x04;

    Profile victim = Warrior();
    victim.armor = 8000;

    const Matchup m =
        Matchup::Build(boss, victim, Hand::Main, Situation());
    const HitTable table = HitTable::OneRoll(m);

    // Roll into the normal band.
    ScriptedRng rng(HUNDRED_PERCENT - 1, 500, 0.5f);
    const Strike s =
        StrikeResolver::Resolve(m, table, DamageRange{500, 500}, rng);

    REQUIRE(s.outcome == Outcome::Normal);
    CHECK_EQ(s.afterArmor, s.afterRoll);
    CHECK_EQ(s.applied, 500u);
}

TEST(CombatDodgeCarriesTheRageBasis)
{
    const Matchup m = PlayerVersusBoss();
    const HitTable table = HitTable::OneRoll(m);

    // Land inside the dodge band.
    const Hundredths intoDodge = table.Bound(Outcome::Miss);
    ScriptedRng rng(intoDodge, 200, 0.5f);

    const Strike s =
        StrikeResolver::Resolve(m, table, DamageRange{200, 200}, rng);

    REQUIRE(s.outcome == Outcome::Dodge);
    CHECK_EQ(s.applied, 0u);

    // The swing was answered, not absent: what it would have carried is the
    // rage basis. The old path left the attacker with nothing at all here.
    CHECK_EQ(s.clean, s.afterArmor);
    CHECK(s.clean > 0);
}

TEST(CombatMissCarriesNothing)
{
    const Matchup m = PlayerVersusBoss();
    const HitTable table = HitTable::OneRoll(m);

    ScriptedRng rng(0, 200, 0.5f);
    const Strike s =
        StrikeResolver::Resolve(m, table, DamageRange{200, 200}, rng);

    REQUIRE(s.outcome == Outcome::Miss);
    CHECK_EQ(s.applied, 0u);
    CHECK_EQ(s.clean, 0u);
}

TEST(CombatBlockIsFlatAndComesAfterArmour)
{
    Profile boss = Boss();
    boss.blockValue = 100;
    boss.armor      = 0;

    const Matchup m =
        Matchup::Build(Warrior(), boss, Hand::Main, Situation());
    const HitTable table = HitTable::OneRoll(m);

    const Hundredths intoBlock = table.Bound(Outcome::Glancing);
    ScriptedRng rng(intoBlock, 250, 0.5f);

    const Strike s =
        StrikeResolver::Resolve(m, table, DamageRange{250, 250}, rng);

    REQUIRE(s.outcome == Outcome::Block);
    CHECK_EQ(s.blocked, 100u);
    CHECK_EQ(s.applied, 150u);
    CHECK_EQ(s.clean, 100u);
}

TEST(CombatCritDoublesBeforeArmour)
{
    Profile boss = Boss();
    boss.armor = 0;

    const Matchup m =
        Matchup::Build(Warrior(), boss, Hand::Main, Situation());
    const HitTable table = HitTable::OneRoll(m);

    const Hundredths intoCrit = table.Bound(Outcome::Block);
    ScriptedRng rng(intoCrit, 300, 0.5f);

    const Strike s =
        StrikeResolver::Resolve(m, table, DamageRange{300, 300}, rng);

    REQUIRE(s.outcome == Outcome::Crit);
    CHECK_EQ(s.raw, 300u);
    CHECK_EQ(s.afterRoll, 600u);
    CHECK_EQ(s.applied, 600u);
}

TEST(CombatGlanceWindowAgainstBossIsFiftyFiveToSeventyFive)
{
    const Matchup m = PlayerVersusBoss();

    // 1.3 - 0.05 * 15 and 1.2 - 0.03 * 15. This part of the curve is right
    // for 2.4.3; what is wrong is the chance, below.
    CHECK(Near(m.glanceLow, 0.55f, 0.0001f));
    CHECK(Near(m.glanceHigh, 0.75f, 0.0001f));
}

TEST(CombatGlanceHasNoCasterPenalty)
{
    // The old core subtracted 0.7 from the low end and 0.3 from the high end
    // for shamans, priests, mages, warlocks and druids. That is not a 2.4.3
    // rule and it is not represented here: Profile carries no class, so there
    // is nowhere for the invention to live. A caster and a warrior at the same
    // skill get the same window.
    const Matchup m = PlayerVersusBoss();
    CHECK(Near(m.glanceLow, 0.55f, 0.0001f));
    CHECK(Near(m.glanceHigh, 0.75f, 0.0001f));
}

TEST(CombatAbsorbAndResistCannotOverdraw)
{
    Strike s;
    s.outcome    = Outcome::Normal;
    s.raw        = 100;
    s.afterRoll  = 100;
    s.afterArmor = 100;
    s.applied    = 100;

    s.ApplyAbsorbResist(80, 500);

    CHECK_EQ(s.absorbed, 80u);
    CHECK_EQ(s.resisted, 20u);
    CHECK_EQ(s.applied, 0u);
    CHECK_EQ(s.clean, 100u);
    CHECK(s.finalised);
}

// ---------------------------------------------------------------------------
// Numbers known to be wrong for 2.4.3.
//
// These pass today and are MEANT to fail when the numbers are corrected. The
// names say so. Do not "fix" them by editing the expectation alone.
// ---------------------------------------------------------------------------

TEST(CombatGlanceChanceIsStillVanilla)
{
    // 2.4.3 from patch 2.1 is 10% + 2% per point of skill gap, capped at 40%.
    // This core still carries Vanilla's 1% per point capped at 25%, which
    // against a +3 boss is 25% of white swings instead of 40%.
    CHECK_EQ(Constants::GLANCE_PER_SKILL_POINT, 100);
    CHECK_EQ(Constants::GLANCE_CAP, 2500);
    CHECK_EQ(PlayerVersusBoss().glance, 2500);
}

TEST(CombatCritSuppressionIsStillUnsourced)
{
    // 20% crit, less 0.04% per point of the 15-point defence gap: 19.4%.
    //
    // Live 2.4.3 suppresses raid-boss crit considerably harder -- somewhere
    // between 3% and 4.8% depending on which source you believe. Nobody has
    // produced a citation, so the number stays where it is rather than being
    // guessed at, and this test holds it still.
    CHECK_EQ(PlayerVersusBoss().crit, 1940);
}

TEST(CombatCreatureBlockIsStillFivePercentFlat)
{
    // Finding six, half fixed. Whether a creature blocks at all is now a
    // capability the builder decides from its equipment; how much it blocks
    // for is still a flat five percent for everything that does.
    CHECK_EQ(Constants::CREATURE_BLOCK_BASE, 500);
    CHECK_EQ(Constants::CREATURE_PARRY_BASE, 500);
}
