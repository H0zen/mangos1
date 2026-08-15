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

// Every DB script a live world has, lowered to MAI.
//
// The fixture is `db_scripts` exported from a running one_world database --
// 2,076 rows using 36 of the 47 commands -- and not a set of examples written
// to make this pass. That distinction is the whole value of the test: a
// migration proved against its author's own imagination proves nothing, and
// the rows a real server carries are the ones with the awkward shapes in them.
//
// What is asserted is coverage and fidelity, not behaviour: every row lowers,
// and every field arrives where the manifest says it should. Whether the two
// engines then DO the same thing is the differential test, which needs a world
// to run in and belongs elsewhere. This is the half that can be checked with
// nothing but the data.

#include "TestHarness.h"

#include "mai/MaiCompile.h"
#include "mai/MaiLowering.h"
#include "mai/MaiTargeting.h"
#include "mai/MaiRunner.h"
#include "mai/MaiParse.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <initializer_list>
#include <map>
#include <utility>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    /// One row of the export, in the column order the query asked for.
    struct Row
    {
        uint32 scriptType = 0;
        uint32 id = 0;
        ScriptInfo info;
    };

    bool ParseRow(std::string const& line, Row& out)
    {
        std::istringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, '\t'))
        {
            cells.push_back(cell);
        }

        if (cells.size() != 17)
        {
            return false;
        }

        std::size_t at = 0;
        auto nextU = [&]() { return uint32(std::strtoul(cells[at++].c_str(),
                                                        nullptr, 10)); };
        auto nextI = [&]() { return int32(std::strtol(cells[at++].c_str(),
                                                      nullptr, 10)); };
        auto nextF = [&]() { return float(std::strtod(cells[at++].c_str(),
                                                      nullptr)); };

        out = Row();
        out.scriptType = nextU();
        out.id = nextU();
        out.info.delay = nextU();
        out.info.command = nextU();
        out.info.raw.data[0] = nextU();
        out.info.raw.data[1] = nextU();
        out.info.buddyEntry = nextU();
        out.info.searchRadiusOrGuid = nextU();
        out.info.data_flags = uint8(nextU());
        for (int i = 0; i < MAX_TEXT_ID; ++i)
        {
            out.info.textId[i] = nextI();
        }
        out.info.x = nextF();
        out.info.y = nextF();
        out.info.z = nextF();
        out.info.o = nextF();
        out.info.id = out.id;
        return true;
    }

    std::string FixturePath()
    {
        // Set by CMake so the test can be run from anywhere; falling back to
        // the source-relative path keeps it runnable by hand.
        if (char const* fromCMake = std::getenv("MANGOS_TEST_DATA"))
        {
            return std::string(fromCMake) + "/db_scripts.tsv";
        }
        return "data/db_scripts.tsv";
    }
}

TEST(MaiLowering_EveryLiveRowLowers)
{
    std::ifstream file(FixturePath().c_str());
    REQUIRE(file.is_open());

    std::string line;
    std::size_t rows = 0;
    std::size_t lowered = 0;
    std::set<uint32> commands;
    std::set<uint32> refused;
    std::string firstError;

    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        Row row;
        REQUIRE(ParseRow(line, row));
        ++rows;
        commands.insert(row.info.command);

        mai::Step step;
        std::string error;
        if (mai::Lower(row.info, step, error))
        {
            ++lowered;

            // The verb is the command, unchanged. That is the promise the
            // manifest makes by keeping the numbers, and it is worth checking
            // rather than assuming: if it ever stopped being true, every
            // existing row would migrate to the wrong action silently.
            CHECK(uint32(step.action) == row.info.command);
            CHECK(step.atMs == row.info.delay * 1000);

            // The buddy search copies across whatever the verb is.
            CHECK(step.buddy.entry == row.info.buddyEntry);
            CHECK(step.buddy.guidOrRadius == row.info.searchRadiusOrGuid);
            CHECK(step.buddy.flags == row.info.data_flags);

            mai::ActionSpec const* spec = mai::SpecOf(step.action);
            REQUIRE(spec != nullptr);
            CHECK(spec->arity <= mai::MaxOperands);

            // The verb's own parameters are the two datalongs, in order --
            // converted where the manifest declares a float, because a DB row
            // keeps a distance in a uint32 like everything else.
            for (std::size_t i = 0; i < spec->own && i < 2; ++i)
            {
                if (spec->params[i].type == mai::ParamType::F32)
                {
                    CHECK(step.operands[i].f == float(row.info.raw.data[i]));
                }
                else
                {
                    CHECK(step.operands[i].u == row.info.raw.data[i]);
                }
            }

            // A position, when the verb takes one, is the row's own.
            if (spec->facets & mai::FacetAt)
            {
                std::size_t const base = spec->arity - 4;
                CHECK(step.operands[base + 0].f == row.info.x);
                CHECK(step.operands[base + 1].f == row.info.y);
                CHECK(step.operands[base + 2].f == row.info.z);
                CHECK(step.operands[base + 3].f == row.info.o);
            }

            // Texts, when it takes them, start right after its own.
            if (spec->facets & mai::FacetTexts)
            {
                for (int i = 0; i < MAX_TEXT_ID; ++i)
                {
                    CHECK(step.operands[spec->own + i].i
                                == row.info.textId[i]);
                }
            }
        }
        else
        {
            refused.insert(row.info.command);
            if (firstError.empty())
            {
                firstError = error;
            }
        }
    }

    std::printf("  db_scripts: %u rows, %u distinct commands, %u lowered\n",
                uint32(rows), uint32(commands.size()), uint32(lowered));

    if (!refused.empty())
    {
        std::printf("  refused commands:");
        for (uint32 command : refused)
        {
            std::printf(" %u", command);
        }
        std::printf("\n  first error: %s\n", firstError.c_str());
    }

    // The fixture is a real export; an empty one means the file moved, not
    // that everything passed.
    CHECK(rows > 2000);
    CHECK(commands.size() >= 36);
    CHECK(refused.empty());
    CHECK(lowered == rows);
}

TEST(MaiLowering_ChainsSortByTime)
{
    // A sequence must be walkable in order, because the runner stops at the
    // first step not yet due. An unsorted chain would silently drop everything
    // after the first row that arrived out of order.
    // Delays in SECONDS, as the column is; the lowering turns them into the
    // milliseconds the runner counts in.
    ScriptChain chain;
    for (uint32 delay : { 5u, 0u, 2u, 0u })
    {
        ScriptInfo row;
        row.delay = delay;
        row.command = 26;               // attack_start: no parameters
        chain.push_back(row);
    }

    mai::Sequence sequence;
    std::string error;
    REQUIRE(mai::Lower(chain, 1, "test", sequence, error));
    REQUIRE(sequence.steps.size() == 4);

    for (std::size_t i = 1; i < sequence.steps.size(); ++i)
    {
        CHECK(sequence.steps[i - 1].atMs <= sequence.steps[i].atMs);
    }

    CHECK(sequence.Duration() == 5000);   // 5 seconds, in ms
}

// ---- who a step acts on -----------------------------------------------------
//
// The four rearranging flags, enumerated. This is the part of the DB scripts
// most likely to be reimplemented subtly wrong, because each flag acts on the
// result of the one before it and no combination is exercised by a test today
// -- only by a server, and only for the combinations that happen to be in the
// tables.
//
// The expected values below are read off the original GetScriptProcessTargets
// in ScriptAction.cpp, by hand, and are the specification here: if MAI and it
// disagree, this is the file that says which one is wrong.

namespace
{
    enum Actor { None = 0, Src = 1, Tgt = 2, Bud = 3 };

    struct Expected
    {
        uint8 flags;
        Actor buddy;        ///< what the search found, if anything
        Actor source;       ///< what the step should end up acting AS
        Actor target;       ///< and ON
    };
}

TEST(MaiTargeting_FlagsRearrangeExactlyAsTheDbScriptsDo)
{
    static Expected const cases[] =
    {
        // No buddy: the flags still rearrange source and target.
        { 0,                                        None, Src, Tgt },
        { mai::ReverseDirection,                    None, Tgt, Src },
        { mai::SourceTargetsSelf,                   None, Src, Src },
        { mai::ReverseDirection |
          mai::SourceTargetsSelf,                   None, Tgt, Tgt },

        // A buddy, and no flag saying what to do with it: it REPLACES the
        // source. This is the default and the commonest row shape there is --
        // "have that creature over there do this".
        { 0,                                        Bud,  Bud, Tgt },
        { mai::ReverseDirection,                    Bud,  Tgt, Bud },
        { mai::SourceTargetsSelf,                   Bud,  Bud, Bud },

        // BuddyAsTarget: the source keeps acting, but on the buddy.
        { mai::BuddyAsTarget,                       Bud,  Src, Bud },
        { mai::BuddyAsTarget |
          mai::ReverseDirection,                    Bud,  Bud, Src },
        { mai::BuddyAsTarget |
          mai::SourceTargetsSelf,                   Bud,  Src, Src },

        // All three at once, which is where an order mistake would show.
        // Reverse runs before self-target, so the source is the buddy and the
        // target follows it -- not the other way round.
        { mai::BuddyAsTarget |
          mai::ReverseDirection |
          mai::SourceTargetsSelf,                   Bud,  Bud, Bud },

        // The flags that select HOW the buddy is found must not rearrange
        // anything on their own.
        { mai::BuddyByGuid,                         Bud,  Bud, Tgt },
        { mai::BuddyIsPet | mai::BuddyIsDespawned,  Bud,  Bud, Tgt },
        { mai::CommandAdditional,                   Bud,  Bud, Tgt },
    };

    for (Expected const& want : cases)
    {
        mai::Cast<int> cast;
        cast.source = Src;
        cast.target = Tgt;
        cast.buddy = want.buddy;

        int source = 0;
        int target = 0;
        mai::Redirect(want.flags, cast, source, target);

        CHECK_EQ(source, int(want.source));
        CHECK_EQ(target, int(want.target));
    }
}

TEST(MaiTargeting_ABuddyThatWasNotFoundLeavesTheSourceAlone)
{
    // Not the same as having no buddy flag: a search that found nothing must
    // fall back to the original source rather than acting on nothing.
    mai::Cast<int> cast;
    cast.source = Src;
    cast.target = Tgt;
    cast.buddy = None;

    int source = 0;
    int target = 0;
    mai::Redirect(0, cast, source, target);

    CHECK_EQ(source, int(Src));
    CHECK_EQ(target, int(Tgt));
}

// ---- advancing a sequence through time --------------------------------------

namespace
{
    /// A sequence whose steps fire at the given times, each a distinct verb-less
    /// action so a trace can be compared by time alone.
    mai::Sequence At(std::initializer_list<uint32> times)
    {
        mai::Sequence sequence;
        sequence.name = "test";
        for (uint32 at : times)
        {
            mai::Step step;
            step.atMs = at;
            step.action = mai::ActionId::AttackStart;
            sequence.steps.push_back(step);
        }
        return sequence;
    }

    /// Every step a tick of @a diff hands out, as their times.
    std::vector<uint32> Tick(mai::Frame& frame, uint32 diff)
    {
        std::vector<uint32> fired;
        mai::Runner run(frame, diff);
        while (mai::Step const* step = run.Next())
        {
            fired.push_back(step->atMs);
        }
        return fired;
    }
}

TEST(MaiRunner_ALongTickRunsEverythingThatCameDueInIt)
{
    // The rule a naive loop breaks: one step per tick would stretch a sequence
    // whose steps are 100ms apart to the length of the server's worst frame.
    mai::Sequence sequence = At({ 0, 100, 200, 300 });
    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<uint32> fired = Tick(frame, 400);
    REQUIRE(fired.size() == 4);
    CHECK_EQ(int(fired[0]), 0);
    CHECK_EQ(int(fired[3]), 300);
    CHECK(frame.Finished());
}

TEST(MaiRunner_StepsSharingATimeAllFireTogetherAndInOrder)
{
    mai::Sequence sequence = At({ 0, 0, 0, 500 });
    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<uint32> fired = Tick(frame, 0);
    CHECK(fired.size() == 3);
    CHECK(!frame.Finished());

    fired = Tick(frame, 500);
    CHECK(fired.size() == 1);
    CHECK(frame.Finished());
}

TEST(MaiRunner_TimeIsAbsoluteSoUnevenTicksDoNotDrift)
{
    // Elapsed accumulates and is compared against each step's own time. Written
    // as a countdown to the next step, a tick longer than one gap would lose
    // the remainder and every later step would drift by it -- which is how a
    // timed encounter falls out of sync with its own dialogue on a busy server.
    mai::Sequence sequence = At({ 1000, 1100, 1200 });
    mai::Frame frame;
    frame.sequence = &sequence;

    CHECK(Tick(frame, 999).empty());
    CHECK(mai::UntilNextMs(frame) == 1);

    // One awkward 250ms tick crosses all three.
    std::vector<uint32> fired = Tick(frame, 250);
    CHECK(fired.size() == 3);
    CHECK(frame.elapsedMs == 1249);
    CHECK(mai::UntilNextMs(frame) == mai::NeverMs);
}

TEST(MaiRunner_StoppingDropsTheRestOfTheSameTick)
{
    // terminate_script ends the run where it is, not at the end of the tick.
    mai::Sequence sequence = At({ 0, 0, 0 });
    mai::Frame frame;
    frame.sequence = &sequence;

    std::size_t seen = 0;
    mai::Runner run(frame, 0);
    while (mai::Step const* step = run.Next())
    {
        (void)step;
        ++seen;
        if (seen == 2)
        {
            run.Stop();
        }
    }

    CHECK(seen == 2);
    CHECK(run.Stopped());
    CHECK(frame.Finished());
}

TEST(MaiRunner_AnInterruptedLoopResumesAfterTheLastStepHandedOut)
{
    // Abandoning the loop mid-tick must not repeat a step that already ran.
    mai::Sequence sequence = At({ 0, 0, 0 });
    mai::Frame frame;
    frame.sequence = &sequence;

    {
        mai::Runner run(frame, 0);
        REQUIRE(run.Next() != nullptr);
        REQUIRE(run.Next() != nullptr);
    }

    CHECK(frame.next == 2);
    CHECK(Tick(frame, 0).size() == 1);
    CHECK(frame.Finished());
}

TEST(MaiRunner_AnEmptyOrAbsentSequenceIsFinishedAndAsksForNoTicks)
{
    mai::Frame nothing;
    CHECK(nothing.Finished());
    CHECK(mai::UntilNextMs(nothing) == mai::NeverMs);

    mai::Sequence empty;
    mai::Frame frame;
    frame.sequence = &empty;
    CHECK(frame.Finished());
    CHECK(Tick(frame, 10000).empty());
    CHECK(empty.Duration() == 0);
}

// ---- the differential -------------------------------------------------------
//
// The point of the whole exercise: MAI must do what Map's schedule does, on
// the schedule's own data, before it is allowed to replace it.
//
// Both are simulated here, from the same tick stream, against the same chains.
// The schedule is modelled from the code that implements it --
//
//     Map::ScriptsStart:   insert(Simulation::Now() + delay * 1000, action)
//     Map::ScriptsProcess: fire while (due <= Simulation::Now())
//
// -- a multimap keyed by the millisecond a step is due, insertion order kept
// among equal keys, everything due at or before now firing in one pass.
//
// Now that the schedule counts in milliseconds too, the comparison is exact:
// same steps, same order, same TICK. It used to be same second, because the
// schedule could not offer better; making the core agree with the clock the
// rest of the map already runs on is what turned an approximate check into a
// strict one.

namespace
{
    /// One firing: which step, and the tick it happened on.
    struct Fired
    {
        std::size_t step;
        std::size_t tick;
    };

    /// A tick stream chosen to be hostile: primes, so no tick lands on a step
    /// boundary and every crossing has to be handled by the comparison rather
    /// than by luck.
    uint32 TickAt(std::size_t index)
    {
        static uint32 const ticks[] = { 37, 401, 1103, 53, 2999, 7, 5003 };
        return ticks[index % (sizeof(ticks) / sizeof(*ticks))];
    }

    /// What Map::m_scriptSchedule would do, simulated.
    std::vector<Fired> ScheduleTrace(ScriptChain const& chain)
    {
        std::multimap<uint64, std::size_t> schedule;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            schedule.insert(std::make_pair(uint64(chain[i].delay) * 1000u, i));
        }

        std::vector<Fired> trace;
        uint64 now = 0;
        std::size_t index = 0;

        for (std::size_t tick = 0; tick < 4000 && !schedule.empty(); ++tick)
        {
            now += TickAt(tick);
            while (!schedule.empty() && schedule.begin()->first <= now)
            {
                trace.push_back(Fired{ index++, tick });
                schedule.erase(schedule.begin());
            }
        }

        return trace;
    }

    /// What MAI does with the same chain and the same ticks.
    std::vector<Fired> MaiTrace(mai::Sequence const& sequence)
    {
        mai::Frame frame;
        frame.sequence = &sequence;

        std::vector<Fired> trace;
        std::size_t index = 0;

        for (std::size_t tick = 0; tick < 4000 && !frame.Finished(); ++tick)
        {
            mai::Runner run(frame, TickAt(tick));
            while (mai::Step const* step = run.Next())
            {
                (void)step;
                trace.push_back(Fired{ index++, tick });
            }
        }

        return trace;
    }
}

TEST(MaiDifferential_MaiRunsTheLiveChainsExactlyAsTheScheduleWould)
{
    std::ifstream file(FixturePath().c_str());
    REQUIRE(file.is_open());

    // Rebuild the chains as the loader would: rows grouped by (type, id).
    std::map<std::pair<uint32, uint32>, ScriptChain> chains;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }
        Row row;
        REQUIRE(ParseRow(line, row));
        chains[std::make_pair(row.scriptType, row.id)].push_back(row.info);
    }

    REQUIRE(!chains.empty());

    std::size_t compared = 0;
    std::size_t steps = 0;
    std::size_t disagreed = 0;

    for (auto const& entry : chains)
    {
        mai::Sequence sequence;
        std::string error;
        REQUIRE(mai::Lower(entry.second, entry.first.second, "chain",
                           sequence, error));

        std::vector<Fired> const wanted = ScheduleTrace(entry.second);
        std::vector<Fired> const got = MaiTrace(sequence);

        bool same = wanted.size() == got.size();
        for (std::size_t i = 0; same && i < wanted.size(); ++i)
        {
            same = wanted[i].step == got[i].step &&
                   wanted[i].tick == got[i].tick;
        }

        if (!same)
        {
            ++disagreed;
            if (disagreed == 1)
            {
                std::printf("  first disagreement: type %u id %u -- schedule "
                            "%u step(s), MAI %u\n",
                            entry.first.first, entry.first.second,
                            uint32(wanted.size()), uint32(got.size()));
            }
        }

        ++compared;
        steps += wanted.size();
    }

    std::printf("  differential: %u chain(s), %u step(s), %u disagreement(s)\n",
                uint32(compared), uint32(steps), uint32(disagreed));

    CHECK(compared > 300);
    CHECK(steps > 2000);
    CHECK(disagreed == 0);
}

// ---- the round trip ---------------------------------------------------------
//
// 686 SQL files were generated from db_scripts. This is the proof that they
// say the same thing: every converted step is parsed back through the manifest
// and compared, field by field, against the step the original row lowers to.
//
// It closes the gap the conversion would otherwise leave. The lowering is
// tested, the runner is tested, the schedule is tested against the runner --
// and none of that says a word about whether the text in `params` means what
// the datalongs meant. A conversion nobody checks is a rewrite with extra
// steps.

TEST(MaiConversion_TheGeneratedSqlSaysWhatTheRowsSaid)
{
    // What the original rows lower to, keyed the way the converter groups them.
    std::map<std::pair<uint32, uint32>, std::vector<mai::Step>> expected;
    {
        std::ifstream file(FixturePath().c_str());
        REQUIRE(file.is_open());

        std::map<std::pair<uint32, uint32>, ScriptChain> chains;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty())
            {
                continue;
            }
            Row row;
            REQUIRE(ParseRow(line, row));
            chains[std::make_pair(row.scriptType, row.id)].push_back(row.info);
        }

        for (auto const& entry : chains)
        {
            mai::Sequence sequence;
            std::string error;
            REQUIRE(mai::Lower(entry.second, entry.first.second, "chain",
                               sequence, error));
            expected[entry.first] = sequence.steps;
        }
    }

    // The kinds, in the order the converter numbers them.
    static char const* const kinds[] =
    {
        "quest_start", "quest_end", "spell", "go_use", "go_template_use",
        "creature_death", "creature_movement", "gossip", "event", "internal"
    };

    std::string const path = std::string(std::getenv("MANGOS_TEST_DATA")
                                             ? std::getenv("MANGOS_TEST_DATA")
                                             : "data") + "/mai_converted.tsv";
    std::ifstream file(path.c_str());
    REQUIRE(file.is_open());

    std::size_t rows = 0;
    std::size_t mismatched = 0;
    std::size_t unparsed = 0;
    std::string firstError;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        std::istringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, '\t'))
        {
            cells.push_back(cell);
        }
        REQUIRE(cells.size() == 9);
        ++rows;

        uint32 kind = 0;
        for (uint32 i = 0; i < uint32(sizeof(kinds) / sizeof(*kinds)); ++i)
        {
            if (cells[0] == kinds[i])
            {
                kind = i;
                break;
            }
        }

        uint32 const id = uint32(std::strtoul(cells[1].c_str(), nullptr, 10));
        std::size_t const seq = std::size_t(std::strtoul(cells[2].c_str(),
                                                         nullptr, 10));
        uint32 const atMs = uint32(std::strtoul(cells[3].c_str(), nullptr, 10));

        mai::Step parsed;
        std::string error;
        if (!mai::Parse(cells[4].c_str(), cells[5].c_str(), parsed, error))
        {
            ++unparsed;
            if (firstError.empty())
            {
                firstError = cells[0] + " " + cells[1] + ": " + error;
            }
            continue;
        }

        parsed.atMs = atMs;
        parsed.buddy.entry = uint32(std::strtoul(cells[6].c_str(), nullptr, 10));
        parsed.buddy.guidOrRadius =
            uint32(std::strtoul(cells[7].c_str(), nullptr, 10));
        parsed.buddy.flags = uint8(std::strtoul(cells[8].c_str(), nullptr, 10));

        auto const& want = expected[std::make_pair(kind, id)];
        REQUIRE(seq < want.size());
        mai::Step const& original = want[seq];

        bool same = parsed.action == original.action &&
                    parsed.atMs == original.atMs &&
                    parsed.buddy.entry == original.buddy.entry &&
                    parsed.buddy.guidOrRadius == original.buddy.guidOrRadius &&
                    parsed.buddy.flags == original.buddy.flags;

        // Only the operands the ORIGINAL gave are compared. The conversion
        // drops a zero it never had to write down -- an unset datalong and an
        // absent parameter are the same thing -- so requiring the converted
        // row to carry it back would be demanding it invent one.
        mai::ActionSpec const* spec = mai::SpecOf(original.action);
        REQUIRE(spec != nullptr);
        for (std::size_t slot = 0; same && slot < spec->arity; ++slot)
        {
            if (!original.Has(slot))
            {
                continue;
            }
            if (spec->params[slot].type == mai::ParamType::F32)
            {
                same = parsed.operands[slot].f == original.operands[slot].f;
            }
            else
            {
                same = parsed.operands[slot].u == original.operands[slot].u;
            }
        }

        if (!same)
        {
            ++mismatched;
            if (firstError.empty())
            {
                firstError = cells[0] + " " + cells[1] + " step " + cells[2]
                             + ": " + cells[4] + " " + cells[5];
            }
        }
    }

    std::printf("  conversion: %u step(s), %u unparsed, %u mismatched\n",
                uint32(rows), uint32(unparsed), uint32(mismatched));
    if (!firstError.empty())
    {
        std::printf("  first: %s\n", firstError.c_str());
    }

    CHECK(rows > 2000);
    CHECK(unparsed == 0);
    CHECK(mismatched == 0);
}

TEST(MaiParse_RefusesWhatItCannotUnderstand)
{
    mai::Step step;
    std::string error;

    // A verb that does not exist.
    CHECK(!mai::Parse("cast_spel", "spell=133", step, error));

    // A parameter the verb does not have. Refused, not ignored: an ignored
    // name is a typo that becomes a step quietly doing less than it says.
    CHECK(!mai::Parse("cast_spell", "spel=133", step, error));

    // A value that is not a number.
    CHECK(!mai::Parse("cast_spell", "spell=fireball", step, error));

    // A required parameter left out.
    CHECK(!mai::Parse("cast_spell", "flags=1", step, error));

    // And the shape that is right.
    REQUIRE(mai::Parse("cast_spell", "spell=133 flags=2", step, error));
    CHECK(step.action == mai::ActionId::CastSpell);
    CHECK(step.operands[0].u == 133);
    CHECK(step.operands[1].u == 2);
    CHECK(step.Has(0));
    CHECK(step.Has(1));
}

// ---- control flow -----------------------------------------------------------
//
// The other two structures of a program, once they are rows in a table.
//
// What is worth asserting is split in two, and the split is the design: the
// COMPILER decides what a script means and refuses what cannot mean anything,
// at load, with the row named -- and the RUNNER then only follows indices. So
// the tests below are of two kinds, and neither of them needs a world. A
// guard's answer arrives through one virtual call, which here comes out of a
// std::map instead of out of a creature.

namespace
{
    /// A Sight backed by a table. Everything a branch needs to know about the
    /// world, in the amount the world is asked for it.
    struct FakeSight : public mai::Sight
    {
        std::map<uint32, uint32> states;

        /// The creature's phase, which is not a state slot and is why
        /// GuardPhase exists -- see the guard tests below.
        uint32 phase = 0;

        /// Nothing can be asked at all -- what a sequence the world started
        /// has to say about a creature's memory.
        bool blind = false;

        bool Ask(mai::Guard const& guard, uint32& held) const override
        {
            if (blind)
            {
                return false;
            }

            if (guard.of == mai::GuardPhase)
            {
                held = phase;
                return true;
            }

            if (guard.of != mai::GuardState)
            {
                return false;
            }

            std::map<uint32, uint32>::const_iterator found =
                states.find(guard.subject);
            held = found == states.end() ? 0 : found->second;
            return true;
        }
    };

    /// One row, spelt as somebody would type it into `mai_step`.
    struct Line
    {
        uint32      atMs;
        char const* action;
        char const* params;
        char const* guard;
    };

    /**
     * The rows, through the real parser and the real compiler.
     *
     * Deliberately not hand-built steps: what is being tested includes whether
     * `if` is a verb the parser knows and whether a guard column reads the same
     * as a rule's, and a fixture that filled the fields in directly would be
     * asserting that the test agrees with itself.
     */
    bool Build(mai::Sequence& out, mai::RuleSet& owner,
               std::initializer_list<Line> lines, std::string& error)
    {
        out = mai::Sequence();
        out.name = "test";

        for (Line const& line : lines)
        {
            mai::Step step;
            if (!mai::Parse(line.action, line.params, step, owner, error))
            {
                return false;
            }

            step.atMs = line.atMs;
            step.guardFirst = uint16(out.guards.size());
            if (!mai::ParseGuards(line.guard, out.guards, &owner, error))
            {
                return false;
            }
            step.guardCount = uint8(out.guards.size() - step.guardFirst);

            out.steps.push_back(step);
        }

        return mai::Compile(out, error);
    }

    /// Which steps a tick handed out, by index -- so a test says which ROW ran
    /// rather than which verb, and two rows with the same verb stay apart.
    std::vector<std::size_t> Ran(mai::Sequence const& sequence,
                                 mai::Frame& frame, uint32 diff,
                                 mai::Sight const* sight,
                                 bool* exhausted = nullptr)
    {
        std::vector<std::size_t> ran;

        mai::Runner run(frame, diff, sight);
        while (mai::Step const* step = run.Next())
        {
            ran.push_back(std::size_t(step - sequence.steps.data()));
        }

        if (exhausted)
        {
            *exhausted = run.Exhausted();
        }

        return ran;
    }
}

TEST(MaiCompile_ASequenceWithNoControlIsLeftExactlyAsItWas)
{
    // The whole reason this is safe to add to a live table: a script that does
    // not branch is not a program, is not reordered, and carries no jumps.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0,    "talk", "text0=-1", "" },
                    { 1000, "talk", "text0=-2", "" } }, error));

    CHECK(!sequence.program);
    CHECK(!mai::Branches(sequence.steps));
    CHECK(sequence.steps[0].flow == mai::FlowNone);
    CHECK(sequence.steps[0].jump == mai::NoJump);
    CHECK(sequence.Duration() == 1000);
}

TEST(MaiCompile_RefusesBlocksThatDoNotBalance)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    // An `if` that is never closed. Running its body unconditionally is the
    // one outcome worse than the script not running at all.
    CHECK(!Build(sequence, owner,
                 { { 0, "if",   "", "phase=2" },
                   { 0, "talk", "text0=-1", "" } }, error));

    CHECK(!Build(sequence, owner,
                 { { 0, "else", "", "" } }, error));

    CHECK(!Build(sequence, owner,
                 { { 0, "end", "", "" } }, error));

    // Two `else` arms on one `if`.
    CHECK(!Build(sequence, owner,
                 { { 0, "if",   "", "phase=2" },
                   { 0, "else", "", "" },
                   { 0, "else", "", "" },
                   { 0, "end",  "", "" } }, error));

    // A guard on `else` would be an `else if` with half of one decision in
    // each of two rows.
    CHECK(!Build(sequence, owner,
                 { { 0, "if",   "", "phase=2" },
                   { 0, "else", "", "phase=3" },
                   { 0, "end",  "", "" } }, error));
}

TEST(MaiCompile_RefusesBreakAndContinueWithNoLoopRoundThem)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    CHECK(!Build(sequence, owner, { { 0, "break", "", "" } }, error));
    CHECK(!Build(sequence, owner, { { 0, "continue", "", "" } }, error));

    // An `if` is not a loop, which is the mistake worth catching: it is the
    // one block a `break` looks like it should leave.
    CHECK(!Build(sequence, owner,
                 { { 0, "if",    "", "phase=2" },
                   { 0, "break", "", "" },
                   { 0, "end",   "", "" } }, error));
}

TEST(MaiCompile_RefusesLoopsNestedDeeperThanAFrameCanCount)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    // Four is the ceiling, and it is a ceiling rather than a stack because the
    // counter slot is handed out here rather than pushed at run time.
    REQUIRE(Build(sequence, owner,
                  { { 0, "repeat", "times=2", "" },
                    { 0, "repeat", "times=2", "" },
                    { 0, "repeat", "times=2", "" },
                    { 0, "repeat", "times=2", "" },
                    { 0, "talk",   "text0=-1", "" },
                    { 0, "end", "", "" }, { 0, "end", "", "" },
                    { 0, "end", "", "" }, { 0, "end", "", "" } }, error));

    CHECK(!Build(sequence, owner,
                 { { 0, "repeat", "times=2", "" },
                   { 0, "repeat", "times=2", "" },
                   { 0, "repeat", "times=2", "" },
                   { 0, "repeat", "times=2", "" },
                   { 0, "repeat", "times=2", "" },
                   { 0, "talk",   "text0=-1", "" },
                   { 0, "end", "", "" }, { 0, "end", "", "" },
                   { 0, "end", "", "" }, { 0, "end", "", "" },
                   { 0, "end", "", "" } }, error));
}

TEST(MaiCompile_RefusesTimeThatRunsBackwardsExceptRoundALoop)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    // Reached from a step five seconds later, this would run the instant it
    // was reached rather than when it says -- silently, and only on one arm.
    CHECK(!Build(sequence, owner,
                 { { 0,    "if",   "", "phase=2" },
                   { 5000, "talk", "text0=-1", "" },
                   { 0,    "talk", "text0=-2", "" },
                   { 5000, "end",  "", "" } }, error));

    // Round a loop it is not backwards, it is the next turn -- and the times
    // inside the body are that turn's own.
    REQUIRE(Build(sequence, owner,
                  { { 0,    "repeat", "times=3", "" },
                    { 0,    "talk",   "text0=-1", "" },
                    { 1000, "end",    "", "" },
                    { 1000, "talk",   "text0=-2", "" } }, error));
}

TEST(MaiRunner_AnIfRunsOneArmAndOnlyOne)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "if",   "", "phase=2" },
                    { 0, "talk", "text0=-1", "" },
                    { 0, "else", "", "" },
                    { 0, "talk", "text0=-2", "" },
                    { 0, "end",  "", "" } }, error));

    CHECK(sequence.program);

    FakeSight sight;
    sight.phase = 2;

    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 1);
    CHECK(frame.Finished());

    // And the other way.
    sight.phase = 1;
    mai::Frame other;
    other.sequence = &sequence;

    ran = Ran(sequence, other, 0, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 3);
    CHECK(other.Finished());
}

TEST(MaiRunner_AGuardOnAnOrdinaryStepNeedsNoBlock)
{
    // The same field doing the same thing without the ceremony: `if` for one
    // line. It is also why a timeline can carry guards and stay a timeline.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "talk", "text0=-1", "enraged=1" },
                    { 0, "talk", "text0=-2", "" } }, error));

    CHECK(!sequence.program);

    FakeSight sight;
    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 1);
}

TEST(MaiRunner_AGuardNobodyCanAnswerDoesNotHold)
{
    // A sequence the world started, asked about a creature's memory. Failing
    // closed is the only safe direction: a step that did not run is a bug a
    // log shows, and one that should not have run is a bug a player finds.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "talk", "text0=-1", "enraged=0" } }, error));

    mai::Frame frame;
    frame.sequence = &sequence;
    CHECK(Ran(sequence, frame, 0, nullptr).empty());

    FakeSight blind;
    blind.blind = true;
    mai::Frame other;
    other.sequence = &sequence;
    CHECK(Ran(sequence, other, 0, &blind).empty());
}

TEST(MaiRunner_RepeatRunsItsBodyThatManyTimesAndKeepsTheRemainder)
{
    // The loop, and the property the straight line has always had: elapsed
    // accumulates, and what comes off at each turn is the turn's period -- so
    // a hundred turns do not drift by a hundred hitches.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0,   "repeat", "times=3", "" },
                    { 0,   "talk",   "text0=-1", "" },
                    { 100, "end",    "", "" } }, error));

    FakeSight sight;
    mai::Frame frame;
    frame.sequence = &sequence;

    // The first turn's body is due at once; its `end` is not.
    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 1);

    // One awkward 250ms tick crosses two more turns and keeps the 50ms over.
    ran = Ran(sequence, frame, 250, &sight);
    CHECK_EQ(int(ran.size()), 2);
    CHECK_EQ(int(frame.elapsedMs), 50);
    CHECK(!frame.Finished());

    // The third `end` falls through rather than going round again.
    ran = Ran(sequence, frame, 100, &sight);
    CHECK(ran.empty());
    CHECK(frame.Finished());
}

TEST(MaiRunner_RepeatNoTimesAtAllSkipsTheBody)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "repeat", "times=0", "" },
                    { 0, "talk",   "text0=-1", "" },
                    { 0, "end",    "", "" },
                    { 0, "talk",   "text0=-2", "" } }, error));

    FakeSight sight;
    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 3);
    CHECK(frame.Finished());
}

TEST(MaiRunner_WhileRunsUntilItsGuardTurnsFalse)
{
    // The unbounded one. Nothing in the sequence counts the turns; what ends
    // it is the world changing under it, which is the whole reason it exists.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0,    "while", "", "adds>0" },
                    { 0,    "talk",  "text0=-1", "" },
                    { 1000, "end",   "", "" } }, error));

    FakeSight sight;
    uint32 const adds = uint32(owner.Intern("adds"));
    sight.states[adds] = 2;

    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 1);
    sight.states[adds] = 1;

    ran = Ran(sequence, frame, 1000, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(!frame.Finished());
    sight.states[adds] = 0;

    ran = Ran(sequence, frame, 1000, &sight);
    CHECK(ran.empty());
    CHECK(frame.Finished());
}

TEST(MaiRunner_BreakLeavesTheLoopAndContinueGoesRoundAgain)
{
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0,   "repeat", "times=3", "" },
                    { 0,   "talk",   "text0=-1", "" },
                    { 0,   "break",  "", "stop=1" },
                    { 0,   "talk",   "text0=-2", "" },
                    { 100, "end",    "", "" },
                    { 100, "talk",   "text0=-3", "" } }, error));

    FakeSight sight;
    uint32 const stop = uint32(owner.Intern("stop"));

    // Nothing to stop it: three turns of both lines, then the step after.
    sight.states[stop] = 0;
    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<std::size_t> ran = Ran(sequence, frame, 1000, &sight);
    CHECK_EQ(int(ran.size()), 7);
    CHECK(frame.Finished());

    // And with it: one line, then out of the loop entirely.
    sight.states[stop] = 1;
    mai::Frame stopped;
    stopped.sequence = &sequence;

    ran = Ran(sequence, stopped, 1000, &sight);
    REQUIRE(ran.size() == 2);
    CHECK(ran[0] == 1);
    CHECK(ran[1] == 5);
    CHECK(stopped.Finished());
}

TEST(MaiRunner_AContinueInABoundedLoopStillCountsTheTurn)
{
    // Sent to the top of the loop instead of to its `end`, a `continue` would
    // skip the counter and turn `repeat 2` into a loop with no exit.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "repeat",   "times=2", "" },
                    { 0, "talk",     "text0=-1", "" },
                    { 0, "continue", "", "skip=1" },
                    { 0, "talk",     "text0=-2", "" },
                    { 0, "end",      "", "" } }, error));

    FakeSight sight;
    sight.states[uint32(owner.Intern("skip"))] = 1;

    mai::Frame frame;
    frame.sequence = &sequence;

    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 2);
    CHECK(ran[0] == 1);
    CHECK(ran[1] == 1);
    CHECK(frame.Finished());
}

TEST(MaiRunner_ALoopThatGetsNowhereCannotTakeTheServerWithIt)
{
    // The one guarantee that is not about fidelity. mangosd has one world
    // thread, and a `while` in a table row must not be able to stop it: the
    // frame spends a tick's worth of steps, says so, keeps its place, and the
    // tick goes on.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "while", "", "spin=1" },
                    { 0, "talk",  "text0=-1", "" },
                    { 0, "end",   "", "" } }, error));

    FakeSight sight;
    sight.states[uint32(owner.Intern("spin"))] = 1;

    mai::Frame frame;
    frame.sequence = &sequence;

    bool exhausted = false;
    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight, &exhausted);

    CHECK(exhausted);
    CHECK(!ran.empty());
    CHECK(ran.size() <= std::size_t(mai::MaxStepsPerTick));
    CHECK(!frame.Finished());

    // And the next tick picks it up rather than starting over.
    ran = Ran(sequence, frame, 0, &sight, &exhausted);
    CHECK(exhausted);
    CHECK(!ran.empty());
}

TEST(MaiRunner_NoControlVerbIsEverHandedToWhateverRunsSteps)
{
    // A branch is not a thing that happens in the world. If one ever left the
    // runner it would reach Execute, find no body for `end`, and log a puzzle.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "if",       "", "phase=1" },
                    { 0, "repeat",   "times=2", "" },
                    { 0, "continue", "", "never=1" },
                    { 0, "break",    "", "never=1" },
                    { 0, "talk",     "text0=-1", "" },
                    { 0, "end",      "", "" },
                    { 0, "else",     "", "" },
                    { 0, "talk",     "text0=-2", "" },
                    { 0, "end",      "", "" } }, error));

    FakeSight sight;
    sight.phase = 1;

    mai::Frame frame;
    frame.sequence = &sequence;

    mai::Runner run(frame, 0, &sight);
    std::size_t handed = 0;
    while (mai::Step const* step = run.Next())
    {
        CHECK(!mai::IsControl(step->action));
        ++handed;
    }

    CHECK_EQ(int(handed), 2);
}

TEST(MaiRunner_AContinueInAWhileStillTakesTheWholeTurn)
{
    // `continue` goes to the `end` in BOTH kinds of loop, and in a `while` the
    // reason is the clock rather than a counter: the back edge is what rewinds
    // it by the turn's period, so a `continue` sent to the condition instead
    // rewound by its own position. A `continue` 200ms into a one-second loop
    // made it a 200ms loop; at zero it was a spin that only the fuel stopped.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0,    "while",    "", "spin=1" },
                    { 0,    "talk",     "text0=-1", "" },
                    { 0,    "continue", "", "skip=1" },
                    { 0,    "talk",     "text0=-2", "" },
                    { 1000, "end",      "", "" } }, error));

    FakeSight sight;
    sight.states[uint32(owner.Intern("spin"))] = 1;
    sight.states[uint32(owner.Intern("skip"))] = 1;

    mai::Frame frame;
    frame.sequence = &sequence;

    // One turn, and the `continue` skips the second line rather than the wait.
    std::vector<std::size_t> ran = Ran(sequence, frame, 0, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 1);

    // Still inside the turn at 999ms: the loop has not come round.
    ran = Ran(sequence, frame, 999, &sight);
    CHECK(ran.empty());

    // And at 1000 it does, exactly once.
    ran = Ran(sequence, frame, 1, &sight);
    REQUIRE(ran.size() == 1);
    CHECK(ran[0] == 1);
    CHECK_EQ(int(frame.elapsedMs), 0);
}

TEST(MaiGuard_PhaseIsThePhaseAndNotAStateThatHappensToBeCalledOne)
{
    // `phase` looks exactly like a bare state name, and it is not one: it is
    // what `set_phase` writes. Interned as a state it was a guard on a slot
    // nothing ever wrote -- zero for ever -- and it was the guard both the
    // schema and the manual used as their worked example.
    mai::RuleSet owner;
    mai::Sequence sequence;
    std::string error;

    REQUIRE(Build(sequence, owner,
                  { { 0, "talk", "text0=-1", "phase=2" } }, error));

    REQUIRE(sequence.guards.size() == 1);
    CHECK(sequence.guards[0].of == mai::GuardPhase);

    // Nothing was interned, so the name did not quietly take a slot as well.
    CHECK(owner.stateNames.empty());

    FakeSight sight;
    mai::Frame frame;
    frame.sequence = &sequence;

    sight.phase = 1;
    CHECK(Ran(sequence, frame, 0, &sight).empty());

    mai::Frame again;
    again.sequence = &sequence;
    sight.phase = 2;
    CHECK(Ran(sequence, again, 0, &sight).size() == 1);

    // And the other direction: a step may not create a second thing with that
    // name, or the guard above and the memory below would be two answers.
    mai::RuleSet other;
    mai::Sequence refused;
    CHECK(!Build(refused, other,
                 { { 0, "set_state", "name=phase value=2", "" } }, error));
}
