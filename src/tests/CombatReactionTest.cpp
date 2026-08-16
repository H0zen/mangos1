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
 * The reaction queue: what used to happen in the middle of a swing.
 *
 * Every case here is one of the audit's use-after-free findings, restated as
 * something the queue makes impossible rather than something a reviewer has
 * to notice.
 */

#include "TestHarness.h"

#include "combat/ReactionQueue.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using namespace Combat;

namespace
{
    const ObjectGuid ATTACKER = ObjectGuid(HIGHGUID_PLAYER, uint32(1));
    const ObjectGuid VICTIM   = ObjectGuid(HIGHGUID_UNIT, uint32(2), uint32(3));
    const ObjectGuid THIRD    = ObjectGuid(HIGHGUID_UNIT, uint32(2), uint32(4));

    /// Records what ran, and can be told to push more while it runs.
    class RecordingSink : public ReactionSink
    {
        public:
            void Run(Reaction const& reaction, ReactionQueue& queue) override
            {
                ran.push_back(reaction);

                if (pushOnRun > 0)
                {
                    --pushOnRun;

                    Reaction next;
                    next.source = reaction.source;
                    next.target = reaction.target;
                    next.depth  = static_cast<std::uint8_t>(reaction.depth + 1);
                    next.what   = ExtraSwing{Hand::Off, 1};

                    accepted = queue.Push(next);
                }
            }

            std::vector<Reaction> ran;
            int  pushOnRun = 0;
            bool accepted  = false;
    };

    Reaction Swing(ObjectGuid source, ObjectGuid target, Hand hand,
                   std::uint32_t count)
    {
        Reaction reaction;
        reaction.source = source;
        reaction.target = target;
        reaction.what   = ExtraSwing{hand, count};
        return reaction;
    }
}

TEST(ReactionQueueRunsInOrder)
{
    ReactionQueue queue;
    RecordingSink sink;

    CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Main, 1)));
    CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Off, 2)));

    queue.Drain(sink);

    REQUIRE(sink.ran.size() == 2);
    CHECK(std::get<ExtraSwing>(sink.ran[0].what).hand == Hand::Main);
    CHECK(std::get<ExtraSwing>(sink.ran[1].what).hand == Hand::Off);
    CHECK_EQ(queue.Pending(), std::size_t(0));
}

TEST(ReactionQueueCarriesEveryKind)
{
    // Five shapes, known at compile time, held by value in a variant. No
    // hierarchy, no allocation, and a visit that the compiler checks is
    // exhaustive.
    ReactionQueue queue;
    RecordingSink sink;

    Reaction proc;
    proc.source = ATTACKER;
    proc.target = VICTIM;
    proc.what   = ProcCast{25504, 0, true};

    Reaction shield;
    shield.source = VICTIM;
    shield.target = ATTACKER;
    shield.what   = DamageShield{467, 20, 0x08};

    Reaction poison;
    poison.source = ATTACKER;
    poison.target = VICTIM;
    poison.what   = ItemCombat{Hand::Off};

    Reaction daze;
    daze.source = VICTIM;
    daze.target = ATTACKER;
    daze.what   = Daze{};

    CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Main, 2)));
    CHECK(queue.Push(proc));
    CHECK(queue.Push(shield));
    CHECK(queue.Push(poison));
    CHECK(queue.Push(daze));

    queue.Drain(sink);

    REQUIRE(sink.ran.size() == 5);
    CHECK(std::holds_alternative<ExtraSwing>(sink.ran[0].what));
    CHECK(std::holds_alternative<ProcCast>(sink.ran[1].what));
    CHECK(std::holds_alternative<DamageShield>(sink.ran[2].what));
    CHECK(std::holds_alternative<ItemCombat>(sink.ran[3].what));
    CHECK(std::holds_alternative<Daze>(sink.ran[4].what));

    CHECK_EQ(std::get<DamageShield>(sink.ran[2].what).amount, 20u);
}

TEST(ReactionQueueStopsAChainAtTheDepthLimit)
{
    // A proc that grants a swing that procs again is the extra-on-extra
    // explosion the old core guarded against with a special case. Here the
    // depth is a property of the reaction, so the guard is one comparison and
    // applies to every kind.
    ReactionQueue queue;
    RecordingSink sink;
    sink.pushOnRun = 10;

    CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Main, 1)));
    queue.Drain(sink);

    // Depth 0 runs, pushes 1; 1 runs, pushes 2; 2 runs, pushes 3 -- refused.
    CHECK_EQ(sink.ran.size(), std::size_t(ReactionQueue::MAX_DEPTH + 1));
    CHECK(!sink.accepted);
    CHECK(queue.Refused() > 0);
}

TEST(ReactionQueueRefusesTooDeepOnPush)
{
    ReactionQueue queue;

    Reaction tooDeep = Swing(ATTACKER, VICTIM, Hand::Main, 1);
    tooDeep.depth = ReactionQueue::MAX_DEPTH + 1;

    CHECK(!queue.Push(tooDeep));
    CHECK_EQ(queue.Pending(), std::size_t(0));
    CHECK_EQ(queue.Refused(), 1u);
}

TEST(ReactionQueueForgetsADeadUnitOnEitherSide)
{
    // The finding this exists for: a damage shield kills the attacker inside
    // the loop that is dealing it, and the next iteration works on a corpse.
    // Nothing tracks lifetimes here -- the reaction simply stops existing.
    ReactionQueue queue;
    RecordingSink sink;

    CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Main, 1)));
    CHECK(queue.Push(Swing(VICTIM, ATTACKER, Hand::Main, 1)));
    CHECK(queue.Push(Swing(THIRD, VICTIM, Hand::Main, 1)));
    CHECK(queue.Push(Swing(THIRD, THIRD, Hand::Main, 1)));

    queue.DropInvolving(VICTIM);

    // Only the pair that never named the victim survives.
    CHECK_EQ(queue.Pending(), std::size_t(1));

    queue.Drain(sink);
    REQUIRE(sink.ran.size() == 1);
    CHECK(sink.ran[0].source == THIRD);
    CHECK(sink.ran[0].target == THIRD);
}

TEST(ReactionQueueHonoursThePerDrainBudget)
{
    ReactionQueue queue;
    RecordingSink sink;

    const std::size_t pushed = ReactionQueue::MAX_PER_DRAIN + 20;
    for (std::size_t i = 0; i < pushed; ++i)
    {
        CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Main, 1)));
    }

    queue.Drain(sink);

    // The budget is a ceiling on work, and crossing it is counted rather than
    // quietly absorbed: a tick that drops reactions should be visible.
    CHECK_EQ(sink.ran.size(), ReactionQueue::MAX_PER_DRAIN);
    CHECK_EQ(queue.Pending(), std::size_t(0));
    CHECK_EQ(queue.Refused(), std::uint32_t(pushed - ReactionQueue::MAX_PER_DRAIN));
}

TEST(ReactionQueueDoesNotReenterItsOwnDrain)
{
    /// A sink that drains the queue it is being drained by.
    class ReentrantSink : public ReactionSink
    {
        public:
            void Run(Reaction const&, ReactionQueue& queue) override
            {
                ++runs;
                queue.Drain(*this);
            }

            int runs = 0;
    };

    ReactionQueue queue;
    ReentrantSink sink;

    CHECK(queue.Push(Swing(ATTACKER, VICTIM, Hand::Main, 1)));
    queue.Drain(sink);

    // One drain owns the queue until it is empty. The inner call returns
    // without doing anything rather than interleaving two traversals.
    CHECK_EQ(sink.runs, 1);
}

TEST(ReactionExtraSwingGrantIsBounded)
{
    // A chain of procs can add to m_extraAttacks without limit. What reaches
    // the queue cannot.
    CHECK_EQ(ExtraSwing::Granted(Hand::Main, 1).count, 1u);
    CHECK_EQ(ExtraSwing::Granted(Hand::Main, 2).count, 2u);
    CHECK_EQ(ExtraSwing::Granted(Hand::Main, 5000).count,
             Constants::MAX_EXTRA_ATTACKS);

    // The hand that earned the grant is the hand that swings.
    CHECK(ExtraSwing::Granted(Hand::Off, 3).hand == Hand::Off);
}

TEST(ReactionQueueClearResetsTheCount)
{
    ReactionQueue queue;

    Reaction tooDeep = Swing(ATTACKER, VICTIM, Hand::Main, 1);
    tooDeep.depth = ReactionQueue::MAX_DEPTH + 1;
    queue.Push(tooDeep);

    CHECK_EQ(queue.Refused(), 1u);

    queue.Clear();

    CHECK_EQ(queue.Refused(), 0u);
    CHECK_EQ(queue.Pending(), std::size_t(0));
}

/**
 * A proc cast carries its own base points, or it does not, and the two are
 * different instructions.
 *
 * The field was an int32 defaulting to zero and the runner never read it, so
 * every reaction that computed an amount would have cast for the spell's own
 * number instead. Nothing pushed one yet, which is the only reason it was not
 * a bug in the world.
 */

TEST(ReactionProcCastWithoutPointsAsksForNone)
{
    ProcCast cast;
    cast.spellId = 25504;

    CHECK(!cast.basePoints.has_value());
}

TEST(ReactionProcCastCarriesAZeroThatMeansZero)
{
    ProcCast cast;
    cast.spellId = 25504;
    cast.basePoints = 0;

    // The distinction the old int32 could not hold: this is "cast for
    // nothing", not "use whatever the spell says".
    CHECK(cast.basePoints.has_value());
    CHECK_EQ(*cast.basePoints, 0);
}

TEST(ReactionProcCastSurvivesTheQueue)
{
    ReactionQueue queue;

    Reaction r;
    r.source = ATTACKER;
    r.target = VICTIM;

    ProcCast cast;
    cast.spellId = 33750;
    cast.basePoints = 137;
    r.what = cast;

    CHECK(queue.Push(r));
    CHECK_EQ(queue.Pending(), std::size_t(1));

    // The variant round-trip is the part worth pinning: a value type in a
    // queue is only as good as what comes back out of it.
    struct Reader : ReactionSink
    {
        std::int32_t seen = -1;
        bool had = false;

        void Run(Reaction const& reaction, ReactionQueue&) override
        {
            if (auto const* c = std::get_if<ProcCast>(&reaction.what))
            {
                had = c->basePoints.has_value();
                seen = c->basePoints ? *c->basePoints : -1;
            }
        }
    } reader;

    queue.Drain(reader);
    CHECK(reader.had);
    CHECK_EQ(reader.seen, 137);
}
