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

#ifndef MANGOS_ISCRIPT_ENGINE_H
#define MANGOS_ISCRIPT_ENGINE_H

#include "ScriptHost.h"

class Creature;
class GameObject;
class CreatureAI;
class GameObjectAI;
class InstanceData;

/**
 * One scripting back end.
 *
 * Nothing in src/game outside the Scripting directory includes this header,
 * and nothing needs to: an engine is reached only through Dispatch(). Adding a
 * second back end -- another interpreter, or the C++ scripts, which are a back
 * end like any other -- is a new implementation of this interface plus one
 * line in ScriptHost.cpp, not an edit anywhere in the world.
 */
namespace scripting
{
    class IEngine
    {
    public:
        virtual ~IEngine() = default;

        /// For log lines and console output; never parsed.
        virtual char const* GetName() const = 0;

        /**
         * Run @a id in the state named by @a ctx.
         *
         * @a args is the caller's payload and may be written back into, but a
         * slot must keep the Kind it arrived with.
         *
         * An engine swallows its own failures: a script error is logged and
         * reported as Verdict::Continue. Nothing from a script may unwind
         * through the world tick.
         */
        virtual Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                                 std::size_t count) = 0;

        /**
         * What this engine offers for @a role over @a subject, or NoBid.
         *
         * Asked of every engine before any of them builds anything, so a
         * loser never constructs an object that is then thrown away. An
         * engine that can answer precisely should. Whether an entry has
         * handlers bound to it at all is a table lookup; building an AI to
         * find out is not.
         */
        virtual int Bid(Context const& ctx, RoleId role, Ref subject)
        {
            (void)ctx; (void)role; (void)subject;
            return NoBid;
        }

        // Only the winning bidder is asked to build. Returning nullptr is
        // allowed and means "I bid but could not build after all" -- the
        // auction then falls to the next bidder, which is not a formality:
        // SD3 bids on having a script bound to the entry, and that script's
        // own GetAI may still decline.

        virtual CreatureAI* MakeCreatureAI(Context const& ctx, Creature* creature)
        {
            (void)ctx; (void)creature;
            return nullptr;
        }

        virtual GameObjectAI* MakeGameObjectAI(Context const& ctx, GameObject* go)
        {
            (void)ctx; (void)go;
            return nullptr;
        }

        virtual InstanceData* MakeInstanceData(Context const& ctx, Map* map)
        {
            (void)ctx; (void)map;
            return nullptr;
        }

        // -- lifetime. Not events, and deliberately not in the manifest: an
        //    engine having its own timers pumped, or being told a state is
        //    gone, is not something that happened in the world. Modelling
        //    them as events would put them in the dispatch table, where every
        //    engine would see another engine's housekeeping.

        /// Give the engine its slice of the tick to run its own timers.
        virtual void Tick(Context const& ctx, uint32 diff)
        {
            (void)ctx; (void)diff;
        }

        /// The state named by @a ctx is going away; drop anything keyed to it.
        virtual void RetireState(Context const& ctx)
        {
            (void)ctx;
        }
    };
}

#endif //MANGOS_ISCRIPT_ENGINE_H
