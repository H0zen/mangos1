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

#include "DbScriptEngine.h"

#include "Map.h"
#include "Object.h"
#include "ScriptMgr.h"

namespace scripting
{
    namespace
    {
        /// EventId -> the DBScriptType the tables are keyed by.
        ///
        /// The manifest numbers the dbscript category from the same values, so
        /// this is the low byte of the id and not a table to keep in step.
        /// Asserting that rather than assuming it is what makes the shortcut
        /// safe to take.
        bool ToScriptType(EventId id, DBScriptType& type)
        {
            uint16 const raw = static_cast<uint16>(id);
            if ((raw >> 8) != 0x0E)
            {
                return false;
            }

            uint16 const local = raw & 0xFF;
            if (local >= DBS_END)
            {
                return false;
            }

            type = static_cast<DBScriptType>(local);
            return true;
        }

        /**
         * Resolve a Ref to the object the schedule will act on.
         *
         * ScriptsStart takes pointers and converts them straight back to
         * guids for the ScriptAction it queues, so this round trip looks
         * wasteful. It is not: it is the check that the object is still on
         * this map before anything is scheduled against it.
         */
        WorldObject* ObjectOn(Context const& ctx, Ref ref)
        {
            if (ref.IsEmpty() || !ctx.map)
            {
                return nullptr;
            }

            return ctx.map->GetWorldObject(ObjectGuid(ref.guid));
        }
    }

    Verdict DbScriptEngine::Dispatch(Context const& ctx, EventId id, Arg* args,
                                     std::size_t count)
    {
        DBScriptType type = DBS_END;
        if (!ToScriptType(id, type))
        {
            return Verdict::Continue;
        }

        MANGOS_ASSERT(count == DbscriptQuestStart::Arity);

        // The schedule belongs to a map. An event raised in the global scope
        // has no schedule to append to, which is not an error -- it simply is
        // not addressed to this engine.
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return Verdict::Continue;
        }

        WorldObject* source = ObjectOn(ctx, args[0].AsEntity());
        WorldObject* target = ObjectOn(ctx, args[1].AsEntity());

        // A script may legitimately have only a source; it may not have
        // neither, because every command resolves its actors from one of them.
        if (!source && !target)
        {
            return Verdict::Continue;
        }

        bool const started = ctx.map->ScriptsStart(
            type,
            static_cast<uint32>(args[2].AsNumber()),
            source,
            target,
            static_cast<Map::ScriptExecutionParam>(args[3].AsNumber()));

        // Whether anything was queued is a value the world reads back, not a
        // verdict: one caller logs an error when a spell effect started no
        // script at all. Nothing has run yet either way.
        args[4] = Arg::FromFlag(started);

        return Verdict::Continue;
    }
}
