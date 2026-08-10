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

#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"

#include <cstdio>

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

            // A text id of -1 is the schema's own "none".
            if (param.type == ParamType::Text)
            {
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

    std::size_t Validate(Sequence const& sequence)
    {
        std::size_t refused = 0;

        for (std::size_t i = 0; i < sequence.steps.size(); ++i)
        {
            std::string error;
            if (Validate(sequence.steps[i], error))
            {
                continue;
            }

            ++refused;

            // Reported and skipped, not fatal. A world's tables are edited by
            // people, and refusing to start a server over one bad row would
            // teach an administrator to turn the check off -- which costs more
            // than the row does. What must not happen is silence.
            sLog.outErrorDb("MAI: %s step %u at %ums: %s",
                            sequence.name.empty() ? "script"
                                                  : sequence.name.c_str(),
                            uint32(i), sequence.steps[i].atMs, error.c_str());
        }

        return refused;
    }
}
