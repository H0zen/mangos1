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

// Checking a script against the world before it is allowed to run.
//
// This is the whole argument for MAI being data rather than a program. A table
// can be held up against the world at load: does spell 12345 exist, is 60123 a
// creature anyone ever spawns, is that text id in the text table. A program
// cannot -- the best it can do is fail at the moment it runs, once, in front of
// whoever happened to be standing there.
//
// The manifest gave every parameter a semantic type for exactly this. Nothing
// here is clever; the value of it is that it happens AT ALL, and at load, and
// names the script and the step that is wrong.

#include "MaiValidate.h"

#include "MaiCompile.h"

#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace mai
{
    namespace
    {
        bool Exists(ParamType type, uint32 value)
        {
            switch (type)
            {
                case ParamType::Spell:
                    return sSpellStore.LookupEntry(value) != nullptr;

                case ParamType::Creature:
                    return ObjectMgr::GetCreatureTemplate(value) != nullptr;

                case ParamType::Gameobject:
                    return ObjectMgr::GetGameObjectInfo(value) != nullptr;

                case ParamType::Item:
                    return ObjectMgr::GetItemPrototype(value) != nullptr;

                case ParamType::Quest:
                    return sObjectMgr.GetQuestTemplate(value) != nullptr;

                case ParamType::Map:
                    return sMapStore.LookupEntry(value) != nullptr;

                case ParamType::Emote:
                    return sEmotesStore.LookupEntry(value) != nullptr;

                case ParamType::Area:
                    return sAreaStore.LookupEntry(value) != nullptr;

                case ParamType::Taxi:
                    return sTaxiPathStore.LookupEntry(value) != nullptr;

                case ParamType::Faction:
                    return sFactionTemplateStore.LookupEntry(value) != nullptr;

                // Sound and movie ids have no store this core loads, and a
                // wrong one is inaudible rather than harmful. Saying so here
                // is better than a check that quietly passes everything.
                case ParamType::Sound:
                case ParamType::Movie:
                default:
                    return true;
            }
        }

        char const* Named(ParamType type)
        {
            switch (type)
            {
                case ParamType::Spell:      return "spell";
                case ParamType::Creature:   return "creature";
                case ParamType::Gameobject: return "gameobject";
                case ParamType::Item:       return "item";
                case ParamType::Quest:      return "quest";
                case ParamType::Map:        return "map";
                case ParamType::Emote:      return "emote";
                case ParamType::Area:       return "area";
                case ParamType::Taxi:   return "taxi path";
                case ParamType::Faction:    return "faction";
                default:                    return "value";
            }
        }
    }

    bool Validate(Step const& step, std::string& error)
    {
        ActionSpec const* spec = SpecOf(step.action);
        if (!spec)
        {
            error = "the step names no action";
            return false;
        }

        for (std::size_t slot = 0; slot < spec->arity; ++slot)
        {
            ParamSpec const& param = spec->params[slot];

            // An absent optional parameter is not a wrong one. This is why the
            // lowering bothered to record which slots were actually given: a
            // creature entry of 0 means "no buddy", not "creature 0".
            if (param.optional && !step.Has(slot))
            {
                continue;
            }

            // A text is the one parameter held as a SIGNED value, and ZERO --
            // only zero -- is its "nothing". NOT -1, however much it looks
            // like a sentinel: text ids in this core are NEGATIVE, the
            // creature-AI range is -2005..-1, and -1 is a line somebody wrote.
            // The lowering learnt that the hard way and sets `given` on any
            // non-zero id for the same reason.
            //
            // Skipped entirely until now, which left the commonest wrong id in
            // the whole system unchecked: a `talk` whose text was never merged
            // loads, validates, and says nothing at all in front of a player --
            // the failure the semantic types were introduced to end, surviving
            // in the one type that names a row rather than a spell.
            if (param.type == ParamType::Text)
            {
                int32 const text = step.operands[slot].i;
                if (text == 0)
                {
                    continue;
                }

                if (!sObjectMgr.GetMangosStringLocale(text))
                {
                    char buffer[192];
                    std::snprintf(buffer, sizeof(buffer),
                                  "%s.%s is %d, which is not a text this world "
                                  "has", spec->name, param.name, text);
                    error = buffer;
                    return false;
                }

                continue;
            }

            uint32 const value = step.operands[slot].u;
            if (value == 0 && param.optional)
            {
                continue;
            }

            if (!Exists(param.type, value))
            {
                char buffer[192];
                std::snprintf(buffer, sizeof(buffer),
                              "%s.%s is %u, which is not a %s this world has",
                              spec->name, param.name, value, Named(param.type));
                error = buffer;
                return false;
            }
        }

        return true;
    }

    std::size_t Validate(Sequence& sequence)
    {
        std::size_t refused = 0;
        std::vector<bool> keep(sequence.steps.size(), true);

        for (std::size_t i = 0; i < sequence.steps.size(); ++i)
        {
            std::string error;
            if (Validate(sequence.steps[i], error))
            {
                continue;
            }

            ++refused;
            keep[i] = false;

            // Reported and skipped, not fatal. A world's tables are edited by
            // people, and refusing to start a server over one bad row would
            // teach an administrator to turn the check off -- which costs more
            // than the row does. What must not happen is silence.
            //
            // NAME THE ROW. This used to lead with `name`, and fall back to
            // the word "script" when there was none -- so a converted sequence,
            // which mostly has no name, reported itself as
            //
            //     MAI: script step 0 at 0ms: update_template.faction is 0 ...
            //
            // twenty-two times in one start-up, and none of the twenty-two
            // said WHICH script. (`kind`, `script`) is the primary key of both
            // `mai_script` and `mai_step`, so leading with it makes every line
            // a row somebody can go and open. The name follows when there is
            // one, because it is the thing a person recognises.
            if (sequence.name.empty())
            {
                sLog.outErrorDb("MAI: %s %u step %u at %ums: %s",
                                sequence.kind, sequence.id, uint32(i),
                                sequence.steps[i].atMs, error.c_str());
            }
            else
            {
                sLog.outErrorDb("MAI: %s %u '%s' step %u at %ums: %s",
                                sequence.kind, sequence.id,
                                sequence.name.c_str(), uint32(i),
                                sequence.steps[i].atMs, error.c_str());
            }
        }

        if (refused == 0)
        {
            return 0;
        }

        // Reported and REMOVED, not fatal. A world's tables are edited by
        // people, and refusing to start a server over one bad row would teach
        // an administrator to turn the check off -- which costs more than the
        // row does. What must not happen is silence, and what must not happen
        // either is the row surviving the report.
        if (Branches(sequence.steps))
        {
            // A program loses all of it. Dropping a row out of one does not
            // leave a shorter program, it leaves a different one: the `end`
            // closes something else, and an `if` whose body has gone still
            // branches -- around nothing, to somewhere that moved.
            sLog.outErrorDb("MAI: %s %u branches and %u of its steps are "
                            "wrong; the whole script is refused, because "
                            "removing a row from a program changes what the "
                            "rest of it means.",
                            sequence.kind, sequence.id, uint32(refused));

            refused = sequence.steps.size();
            sequence.steps.clear();
            sequence.guards.clear();
            return refused;
        }

        std::vector<Step> kept;
        kept.reserve(sequence.steps.size() - refused);
        for (std::size_t i = 0; i < sequence.steps.size(); ++i)
        {
            if (keep[i])
            {
                kept.push_back(sequence.steps[i]);
            }
        }

        // The guards are NOT compacted with them. A step names a window into
        // that table by absolute position, so leaving the removed steps' guards
        // where they are keeps every surviving window pointing at what it
        // pointed at -- for the price of a few unread entries in a vector that
        // is empty on every converted script in the world.
        sequence.steps = std::move(kept);
        return refused;
    }
}
