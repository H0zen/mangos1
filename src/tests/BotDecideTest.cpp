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
 * The BotsNG decision core, tested with no server behind it.
 *
 * THAT IS THE ASSERTION THIS FILE MAKES BEFORE ANY OF ITS CASES DO. It builds
 * `Percept.h`, `Arbiter.cpp`, `Policy.cpp` and one layer, and it links neither
 * `game` nor `shared` for any of them -- the decision layer has no world in it
 * to link. Nothing in the old bot engine could be tested this way: a rule
 * reached the world through `Player*` inside `Execute`, so exercising one
 * meant standing a server up, logging a character in and arranging the state
 * by hand.
 *
 * A case here is a struct literal and an assertion about what came back.
 */

#include "TestHarness.h"

#include "../modules/BotsNG/decide/Policy.h"
#include "../modules/BotsNG/decide/layers/Survival.h"

#include <variant>

namespace
{
    constexpr std::uint32_t ItemHealthstone = 5512;
    constexpr std::uint32_t ItemHealPotion  = 929;
    constexpr std::uint32_t ItemManaPotion  = 3827;
    constexpr std::uint32_t ItemFood        = 4541;
    constexpr std::uint32_t ItemWater       = 1179;

    /// A bot at full health, out of combat, carrying one of everything.
    bots::Perception Stocked()
    {
        bots::Perception p;
        p.self.guid      = 0x1000;
        p.self.level     = 60;
        p.self.health    = 4000;
        p.self.maxHealth = 4000;
        p.self.power     = 3000;
        p.self.powerKind = bots::PowerKind::Mana;

        p.self.carried[0] = {ItemHealthstone, 1, 0, 1, bots::Use::Healthstone};
        p.self.carried[1] = {ItemHealPotion, 5, 0, 41, bots::Use::Heal};
        p.self.carried[2] = {ItemManaPotion, 5, 0, 41, bots::Use::Mana};
        p.self.carried[3] = {ItemFood, 20, 0, 45, bots::Use::Food};
        p.self.carried[4] = {ItemWater, 20, 0, 45, bots::Use::Drink};
        p.self.carriedCount = 5;
        return p;
    }

    bots::Policy SurvivalOnly()
    {
        bots::Policy policy;
        policy.name = "survival only";
        policy.layers.push_back(&bots::SurvivalLayer);
        return policy;
    }

    /// The item entry a plan uses, or zero if it does not use one.
    std::uint32_t UsedItem(bots::Plan const& plan)
    {
        for (bots::Intent const& intent : plan)
        {
            if (auto const* use = std::get_if<bots::UseItem>(&intent.verb))
            {
                return use->itemEntry;
            }
        }
        return 0;
    }

    std::uint32_t CastSpell(bots::Plan const& plan)
    {
        for (bots::Intent const& intent : plan)
        {
            if (auto const* cast = std::get_if<bots::Cast>(&intent.verb))
            {
                return cast->spell;
            }
        }
        return 0;
    }
}

TEST(BotDecide_NothingWantedIsAnAnswer)
{
    // A bot in perfect shape does nothing, and says so by returning an empty
    // plan rather than by falling through to a default action. The old engine
    // could not express this: an empty queue made it push defaults and recurse.
    const bots::Perception p = Stocked();
    const bots::Plan plan = bots::Decide(SurvivalOnly(), p);

    CHECK(plan.Empty());
    CHECK_EQ(plan.count, std::size_t(0));
}

TEST(BotDecide_HealthstoneBeforePotion)
{
    bots::Perception p = Stocked();
    p.self.inCombat  = true;
    p.self.healthPct = 20;

    bots::IntentSink proposals;
    const bots::Plan plan = bots::Decide(SurvivalOnly(), p, &proposals);

    // Both were proposed -- the layer offers everything it would accept ...
    CHECK_EQ(proposals.Count(), std::size_t(2));
    // ... and exactly one survives, because both want the action channel.
    CHECK_EQ(plan.count, std::size_t(1));
    CHECK_EQ(UsedItem(plan), ItemHealthstone);
    CHECK_EQ(plan.rejected, std::size_t(1));
}

TEST(BotDecide_PotionWhenTheStoneIsOnCooldown)
{
    bots::Perception p = Stocked();
    p.self.inCombat  = true;
    p.self.healthPct = 20;
    p.self.carried[0].cooldownLeftMs = 90000;

    bots::IntentSink proposals;
    const bots::Plan plan = bots::Decide(SurvivalOnly(), p, &proposals);

    // The stone is not proposed at all. It is not proposed and rejected: an
    // item on cooldown is never a candidate, which is the whole difference
    // between this design and one that tries and recovers.
    CHECK_EQ(proposals.Count(), std::size_t(1));
    CHECK_EQ(UsedItem(plan), ItemHealPotion);
}

TEST(BotDecide_NoEatingInCombat)
{
    bots::Perception p = Stocked();
    p.self.inCombat  = true;
    p.self.healthPct = 55;   // below eatAt, above every combat threshold

    const bots::Plan plan = bots::Decide(SurvivalOnly(), p);
    CHECK(plan.Empty());
}

TEST(BotDecide_EatThenDrink)
{
    bots::Perception p = Stocked();
    p.self.healthPct = 40;
    p.self.powerPct  = 30;

    bots::IntentSink proposals;
    bots::Plan plan = bots::Decide(SurvivalOnly(), p, &proposals);

    CHECK_EQ(proposals.Count(), std::size_t(2));
    CHECK_EQ(plan.count, std::size_t(1));
    CHECK_EQ(UsedItem(plan), ItemFood);

    // Next tick, having started to eat, the water is what is left to want.
    p.self.eating = true;
    plan = bots::Decide(SurvivalOnly(), p);
    CHECK_EQ(UsedItem(plan), ItemWater);
}

TEST(BotDecide_RageBotNeverDrinks)
{
    bots::Perception p = Stocked();
    p.self.powerKind = bots::PowerKind::Rage;
    p.self.powerPct  = 0;
    p.self.healthPct = 100;

    const bots::Plan plan = bots::Decide(SurvivalOnly(), p);
    CHECK(plan.Empty());
}

TEST(BotDecide_BestCarriedPrefersTheHighestUsableRank)
{
    bots::Perception p;
    p.self.level = 40;
    p.self.carried[0] = {1179, 20, 0, 5, bots::Use::Drink};    // starter water
    p.self.carried[1] = {8766, 20, 0, 35, bots::Use::Drink};   // the good one
    p.self.carried[2] = {28399, 20, 0, 65, bots::Use::Drink};  // too high
    p.self.carriedCount = 3;

    bots::Carried const* best = bots::BestCarried(p.self, bots::Use::Drink);
    REQUIRE(best != nullptr);
    CHECK_EQ(best->itemEntry, std::uint32_t(8766));

    // An empty stack is not a drink, however good it would have been.
    p.self.carried[1].count = 0;
    best = bots::BestCarried(p.self, bots::Use::Drink);
    REQUIRE(best != nullptr);
    CHECK_EQ(best->itemEntry, std::uint32_t(1179));
}

// -- the arbiter, on proposals made by hand ---------------------------------

namespace
{
    void ProposeShadowBolt(bots::Perception const&, bots::IntentSink& sink)
    {
        sink.Propose(bots::ProposeCast(686, 0x2000, bots::ScoreRotation, 200,
                                       "shadow bolt"));
    }

    void ProposeCorruption(bots::Perception const&, bots::IntentSink& sink)
    {
        sink.Propose(bots::ProposeCast(172, 0x2000, bots::ScoreRotation, 100,
                                       "corruption"));
    }

    void ProposeStepUp(bots::Perception const&, bots::IntentSink& sink)
    {
        bots::Intent intent;
        intent.verb  = bots::MoveTo{bots::Point{10.0f, 20.0f, 30.0f}, true};
        intent.score = bots::ScoreFiller;
        intent.why   = "close the gap";
        sink.Propose(intent);
    }
}

TEST(BotDecide_MovingAndCastingAreNotRivals)
{
    bots::Perception p = Stocked();
    p.self.inCombat = true;

    bots::Policy policy;
    policy.layers.push_back(&ProposeShadowBolt);
    policy.layers.push_back(&ProposeStepUp);

    const bots::Plan plan = bots::Decide(policy, p);

    // Two intents, on two channels, in one tick -- which is what a character
    // can really do and what a single "action slot" would have forbidden.
    CHECK_EQ(plan.count, std::size_t(2));
    CHECK_EQ(CastSpell(plan), std::uint32_t(686));
    CHECK_EQ(plan.rejected, std::size_t(0));
}

TEST(BotDecide_EqualScoresAreSettledByPolicyOrder)
{
    bots::Perception p = Stocked();
    p.self.inCombat = true;

    bots::Policy first;
    first.layers.push_back(&ProposeShadowBolt);
    first.layers.push_back(&ProposeCorruption);
    CHECK_EQ(CastSpell(bots::Decide(first, p)), std::uint32_t(686));

    // The same two layers, listed the other way round, decide the other way.
    // Nothing else about the bot changed -- the precedence IS the list.
    bots::Policy second;
    second.layers.push_back(&ProposeCorruption);
    second.layers.push_back(&ProposeShadowBolt);
    CHECK_EQ(CastSpell(bots::Decide(second, p)), std::uint32_t(172));
}

TEST(BotDecide_TheFirstOfManyEqualsWins)
{
    // Two proposals can come out in the right order by luck: a small unstable
    // sort often leaves a pair alone. Eight cannot, so this is the case that
    // actually holds `std::stable_sort` to its promise.
    bots::IntentSink sink;
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        sink.Propose(bots::ProposeCast(700 + i, 0x2000, bots::ScoreRotation, 0,
                                       "one of eight equals"));
    }

    bots::Self self;
    self.power = 10000;
    CHECK_EQ(CastSpell(bots::Arbitrate(self, sink)), std::uint32_t(700));
}

TEST(BotDecide_GlobalCooldownStopsSpellsAndNotItems)
{
    // ONE CANDIDATE AT A TIME FOR THE ACTION CHANNEL, in each half, and that
    // is the whole point of splitting this case in two. Written as a single
    // scene -- a hurt bot with a healthstone AND a spell -- it passed with the
    // global cooldown check deleted, because the stone had taken the channel
    // and the spell was refused for a reason that had nothing to do with the
    // cooldown. A test that cannot be made to fail is not testing anything.
    bots::Perception spellOnly = Stocked();
    spellOnly.self.inCombat  = true;
    spellOnly.self.gcdLeftMs = 800;

    bots::Policy casting;
    casting.layers.push_back(&ProposeShadowBolt);

    // Nothing else wants the channel, so an accepted bolt here could only mean
    // the cooldown was ignored.
    CHECK(bots::Decide(casting, spellOnly).Empty());

    // With the cooldown elapsed, the same scene casts.
    spellOnly.self.gcdLeftMs = 0;
    CHECK_EQ(CastSpell(bots::Decide(casting, spellOnly)), std::uint32_t(686));

    // And an item goes down DURING the cooldown, which is 2.4.3's rule --
    // stated once in the cost of a proposal instead of rediscovered by every
    // action that ever wanted to know.
    bots::Perception itemOnly = Stocked();
    itemOnly.self.inCombat  = true;
    itemOnly.self.healthPct = 20;
    itemOnly.self.gcdLeftMs = 800;

    const bots::Plan plan = bots::Decide(SurvivalOnly(), itemOnly);
    CHECK_EQ(plan.count, std::size_t(1));
    CHECK_EQ(UsedItem(plan), ItemHealthstone);
}

TEST(BotDecide_ACastInFlightIsOnlyInterruptedForSurvival)
{
    bots::Perception p = Stocked();
    p.self.castingSpell = 585;
    p.self.healthPct    = 40;
    p.self.powerPct     = 30;

    // Out of combat and merely peckish: the meal waits for the cast to finish.
    CHECK(bots::Decide(SurvivalOnly(), p).Empty());

    // At survival urgency it does not wait.
    p.self.inCombat  = true;
    p.self.healthPct = 15;
    const bots::Plan plan = bots::Decide(SurvivalOnly(), p);
    CHECK_EQ(UsedItem(plan), ItemHealthstone);
}

TEST(BotDecide_APoorBotDoesNotProposeWhatItCannotPay)
{
    bots::Perception p = Stocked();
    p.self.inCombat = true;
    p.self.power    = 150;   // a bolt costs 200

    bots::Policy policy;
    policy.layers.push_back(&ProposeShadowBolt);
    policy.layers.push_back(&ProposeCorruption);

    const bots::Plan plan = bots::Decide(policy, p);

    // The expensive one is refused and the cheap one takes the channel. The
    // old engine would have popped the bolt, been refused by isPossible(), and
    // pushed an alternative at a slightly higher relevance to try next tick.
    CHECK_EQ(plan.count, std::size_t(1));
    CHECK_EQ(CastSpell(plan), std::uint32_t(172));
}

TEST(BotDecide_TheStunnedWantAndCannot)
{
    bots::Perception p = Stocked();
    p.self.inCombat  = true;
    p.self.healthPct = 10;
    p.self.stunned   = true;

    const bots::Plan plan = bots::Decide(SurvivalOnly(), p);

    CHECK(plan.Empty());
    // Counted, not forgotten: a trace of a stunned bot shows what it wanted.
    CHECK_EQ(plan.rejected, std::size_t(2));
}

TEST(BotDecide_TheDeadProposeNothing)
{
    bots::Perception p = Stocked();
    p.self.dead      = true;
    p.self.healthPct = 0;

    bots::IntentSink proposals;
    const bots::Plan plan = bots::Decide(SurvivalOnly(), p, &proposals);

    CHECK(plan.Empty());
    CHECK_EQ(proposals.Count(), std::size_t(0));
}

TEST(BotDecide_AFullSinkKeepsTheUrgentOnes)
{
    bots::IntentSink sink;
    for (std::size_t i = 0; i < bots::MaxProposals; ++i)
    {
        sink.Propose(bots::ProposeCast(100 + std::uint32_t(i), 0x2000,
                                       bots::ScoreFiller, 0, "filler"));
    }
    CHECK_EQ(sink.Count(), bots::MaxProposals);
    CHECK_EQ(sink.Dropped(), std::size_t(0));

    // The seventeenth is the one that matters, and it displaces a filler
    // rather than being dropped for arriving late.
    sink.Propose(bots::ProposeCast(999, 0x2000, bots::ScoreSurvival, 0,
                                   "the one that matters"));
    CHECK_EQ(sink.Count(), bots::MaxProposals);
    CHECK_EQ(sink.Dropped(), std::size_t(1));

    bots::Self self;
    self.power = 10000;
    const bots::Plan plan = bots::Arbitrate(self, sink);
    CHECK_EQ(CastSpell(plan), std::uint32_t(999));
}
