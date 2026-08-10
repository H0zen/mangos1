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

// Turning a `dbscripts_on_*` row into a MAI step.
//
// This is the whole of the migration, and it is deliberately GENERIC -- forty
// lines rather than a case for each of the forty-seven verbs. It can be,
// because the two representations agree on more than they disagree:
//
//   * the command number IS the verb; the manifest kept the numbers
//   * a row's two datalongs ARE the verb's own parameters, in order, which is
//     the order the manifest declares them in because the manifest was
//     extracted from the very union that defines them
//   * a row's dataint1..4 ARE the `texts` facet, and its x/y/z/o ARE `at`
//   * the buddy fields modify the step, and copy across untouched
//
// A hand-written switch would have been forty-seven chances to transpose two
// datalongs, and no way to notice: both are uint32, both load, and the wrong
// one only shows up as a creature casting the wrong spell in a dungeon nobody
// has run this month. The generated ActionSpec knows how many parameters are
// the verb's own and which facets follow, so this reads that instead of
// knowing anything itself.

#include "MaiLowering.h"

#include <algorithm>
#include <cstdio>

namespace mai
{
    namespace
    {
        /// Whether a row actually supplied an optional parameter.
        ///
        /// Zero is a legitimate value for most of these, so "was it given" is
        /// not answerable from the value -- except that this is exactly what
        /// the DB scripts have always done: an unset datalong is 0 and every
        /// command treats 0 as its own default. Reproducing that is the point;
        /// improving on it here would make the two systems disagree, which is
        /// the one thing this must not do.
        bool Supplied(uint32 raw)
        {
            return raw != 0;
        }
    }

    bool Lower(ScriptInfo const& row, Step& out, std::string& error)
    {
        ActionSpec const* spec = SpecOf(ActionId(row.command));
        if (!spec)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer),
                          "command %u is not a MAI action", row.command);
            error = buffer;
            return false;
        }

        if (spec->arity > MaxOperands)
        {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer),
                          "%s takes %u parameters and a step holds %u",
                          spec->name, uint32(spec->arity),
                          uint32(MaxOperands));
            error = buffer;
            return false;
        }

        out = Step();
        out.atMs = row.delay;
        out.action = spec->id;

        // The buddy search modifies the step whatever the verb is.
        out.buddy.entry = row.buddyEntry;
        out.buddy.guidOrRadius = row.searchRadiusOrGuid;
        out.buddy.flags = row.data_flags;

        std::size_t slot = 0;

        // The verb's own parameters: datalong, then datalong2. A verb with
        // more than two of its own would be one the union could not express,
        // so there are none, and the loop stops of its own accord.
        uint32 const raw[2] = { row.raw.data[0], row.raw.data[1] };
        for (std::size_t i = 0; i < spec->own && i < 2; ++i, ++slot)
        {
            out.operands[slot].u = raw[i];
            if (Supplied(raw[i]) || !spec->params[slot].optional)
            {
                out.given |= uint8(1u << slot);
            }
        }

        // Then the facets, in the order the generator lays them out.
        if (spec->facets & FacetTexts)
        {
            for (int i = 0; i < MAX_TEXT_ID; ++i, ++slot)
            {
                out.operands[slot].i = row.textId[i];
                // A text id of -1 is "none" in this schema, and 0 is a real
                // one, so the test here is not Supplied().
                if (row.textId[i] >= 0)
                {
                    out.given |= uint8(1u << slot);
                }
            }
        }

        if (spec->facets & FacetAt)
        {
            float const at[4] = { row.x, row.y, row.z, row.o };
            for (int i = 0; i < 4; ++i, ++slot)
            {
                out.operands[slot].f = at[i];
                out.given |= uint8(1u << slot);
            }
        }

        if (slot != spec->arity)
        {
            char buffer[192];
            std::snprintf(buffer, sizeof(buffer),
                          "%s filled %u of %u parameters -- the manifest and "
                          "this lowering disagree about its shape",
                          spec->name, uint32(slot), uint32(spec->arity));
            error = buffer;
            return false;
        }

        return true;
    }

    bool Lower(ScriptChain const& chain, uint32 id, char const* name,
               Sequence& out, std::string& error)
    {
        out = Sequence();
        out.id = id;
        out.name = name ? name : "";
        out.steps.reserve(chain.size());

        for (ScriptInfo const& row : chain)
        {
            Step step;
            if (!Lower(row, step, error))
            {
                return false;
            }
            out.steps.push_back(step);
        }

        // A chain is already ordered by delay where it is built, but this does
        // not assume it: the runner walks steps in order and stops at the
        // first one not yet due, so an unsorted chain would silently skip
        // everything after the first out-of-order row.
        std::stable_sort(out.steps.begin(), out.steps.end(),
            [](Step const& a, Step const& b) { return a.atMs < b.atMs; });

        return true;
    }
}
