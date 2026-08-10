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

#include "mai/MaiLowering.h"
#include "mai/MaiTargeting.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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
            CHECK(step.atMs == row.info.delay);

            // The buddy search copies across whatever the verb is.
            CHECK(step.buddy.entry == row.info.buddyEntry);
            CHECK(step.buddy.guidOrRadius == row.info.searchRadiusOrGuid);
            CHECK(step.buddy.flags == row.info.data_flags);

            mai::ActionSpec const* spec = mai::SpecOf(step.action);
            REQUIRE(spec != nullptr);
            CHECK(spec->arity <= mai::MaxOperands);

            // The verb's own parameters are the two datalongs, in order.
            for (std::size_t i = 0; i < spec->own && i < 2; ++i)
            {
                CHECK(step.operands[i].u == row.info.raw.data[i]);
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
    ScriptChain chain;
    for (uint32 delay : { 5000u, 0u, 2000u, 0u })
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

    CHECK(sequence.Duration() == 5000);
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
