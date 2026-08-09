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

#include "ScriptHost.h"
#include "IScriptEngine.h"

#include "Object.h"

#ifdef ENABLE_ELUNA
#include "ElunaEngine.h"
#endif /* ENABLE_ELUNA */

#include <memory>
#include <vector>

namespace scripting
{
    namespace detail
    {
        // The compile-time gate: is there any engine at all in this build?
        //
        // Whether a *state* exists for a given map is a runtime question and
        // belongs to the engine, which answers Continue when it has none. The
        // flag here only spares an emit site the call when the answer can
        // never be anything else.
#ifdef ENABLE_ELUNA
        bool g_scriptsEnabled = true;
#else
        bool g_scriptsEnabled = false;
#endif /* ENABLE_ELUNA */
    }

    namespace
    {
        /// Everything the seam hides: one object, one owner, one place.
        struct HostState
        {
            std::vector<std::unique_ptr<IEngine>> engines;
        };

        HostState MakeHostState()
        {
            HostState state;

#ifdef ENABLE_ELUNA
            state.engines.push_back(std::unique_ptr<IEngine>(new ElunaEngine()));
#endif /* ENABLE_ELUNA */

            return state;
        }

        /**
         * The engines, built once.
         *
         * A function-local static so that construction is ordered by first use
         * rather than by link order, and so the maps -- which update in
         * parallel -- cannot race the build: initialisation of a local static
         * is thread-safe.
         */
        HostState& State()
        {
            static HostState state = MakeHostState();
            return state;
        }
    }

    Ref RefOf(Object const* object)
    {
        return object ? Ref{ object->GetObjectGuid().GetRawValue() } : Ref{ 0 };
    }

    namespace detail
    {
        Context ToContext(WorldObject const* object)
        {
            if (!object || !object->IsInWorld())
            {
                return Context{ Context::Scope::None, nullptr };
            }

            return Context{ Context::Scope::Map, object->GetMap() };
        }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count)
        {
            if (ctx.scope == Context::Scope::None)
            {
                return Verdict::Continue;
            }

            // A refusal ends the chain: once an action has been vetoed there is
            // nothing left for a later engine to weigh in on. So does a claim,
            // but for the opposite reason -- the behaviour has already been
            // produced, and a second engine producing it again would run it
            // twice. Everything else falls through to every engine, which is
            // the difference from today: a Lua handler no longer suppresses
            // the C++ one merely by existing.
            for (std::unique_ptr<IEngine> const& engine : State().engines)
            {
                Verdict const verdict = engine->Dispatch(ctx, id, args, count);
                if (verdict != Verdict::Continue)
                {
                    return verdict;
                }
            }

            return Verdict::Continue;
        }
    }
}
