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

#ifndef MANGOS_SCRIPT_HOST_H
#define MANGOS_SCRIPT_HOST_H

#include "ScriptEvents.h"

#include <type_traits>
#include <utility>

class Map;
class Object;
class WorldObject;
class Creature;
class GameObject;
class CreatureAI;
class GameObjectAI;
class InstanceData;
class Guild;
class Group;
class Quest;
class BattleGround;

/**
 * What a call site in the world includes, and all it includes.
 *
 * A hook is two lines and has no null check and no #ifdef:
 *
 * @code
 *     scripting::PlayerSave event{ scripting::RefOf(this) };
 *     scripting::Notify(this, event);
 * @endcode
 *
 * Everything below the seam -- which engines exist, whether any is loaded,
 * where a given map keeps its state -- is behind Dispatch(), whose definition
 * lives in the one translation unit that is allowed to name an engine. When no
 * engine is compiled in, an emit site costs a single predictable test on a
 * process-wide flag and no call at all.
 */
namespace scripting
{
    /**
     * Names the scripting state an event belongs to.
     *
     * Built at the call site and consumed inside the same call, so the Map it
     * holds cannot go stale: it never outlives the frame that made it.
     */
    struct Context
    {
        enum class Scope : uint8
        {
            None,       ///< nothing is listening; the event is dropped
            Global,     ///< the world-wide state
            Map         ///< the state belonging to `map`
        };

        Scope scope;
        ::Map* map;
    };

    /// The world-wide state, for events that belong to no particular map.
    inline Context GlobalContext()
    {
        return Context{ Context::Scope::Global, nullptr };
    }

    /// The state that belongs to @a map.
    inline Context ContextOf(::Map* map)
    {
        return map ? Context{ Context::Scope::Map, map }
                   : Context{ Context::Scope::None, nullptr };
    }

    /// Identity of @a object; an empty Ref for nullptr.
    Ref RefOf(Object const* object);

    /// Identity of something that has one but is not an ObjectGuid.
    inline Handle HandleOf(Domain domain, uint64 id)
    {
        return id ? Handle{ id, domain } : Handle{ 0, Domain::None };
    }

    Handle HandleOf(Guild const* guild);
    Handle HandleOf(Group const* group);
    Handle HandleOf(Quest const* quest);
    Handle HandleOf(BattleGround const* bg);

    namespace detail
    {
        /**
         * False when no engine is compiled in or none has loaded anything.
         *
         * Read inline at every emit site, which is why it is a plain flag and
         * not a function: the "nobody is listening" path must not become a
         * call in the middle of a hot loop.
         */
        extern bool g_scriptsEnabled;

        /// The single non-template entry point into the engines.
        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count);

        /**
         * The epoch a Borrow handed out right now is stamped with.
         *
         * Bumped by Dispatch when it returns, so every borrow issued during a
         * call is stale the instant that call is over. Thread-local, because
         * maps update in parallel and an epoch shared between them would let
         * one map's dispatch invalidate another's live borrow.
         */
        uint32 CurrentEpoch();

        /// True while @a borrow still refers to something alive.
        bool IsBorrowLive(Borrow const& borrow);
    }

    /// Build a Borrow for @a target, valid until the current dispatch returns.
    template <class T>
    Borrow Lend(Domain domain, T* target)
    {
        return Borrow{ const_cast<void*>(static_cast<void const*>(target)),
                       detail::CurrentEpoch(), domain };
    }

    namespace detail
    {

        inline Context ToContext(Context const& ctx)
        {
            return ctx;
        }

        inline Context ToContext(::Map* map)
        {
            return map ? Context{ Context::Scope::Map, map }
                       : Context{ Context::Scope::None, nullptr };
        }

        /**
         * The state that owns @a object, or None when it is not in the world.
         *
         * Dropping the event for an object outside a map is deliberate and
         * matches what the engines already did with one: there is no state to
         * run it in, and inventing the global state instead would fire hooks
         * that never used to fire.
         */
        Context ToContext(WorldObject const* object);

        template <class Ev>
        Verdict Emit(Context const& ctx, Ev&& event)
        {
            typedef typename std::decay<Ev>::type EventType;

            Arg args[EventType::Arity];
            event.Pack(args);

            Verdict const verdict = Dispatch(ctx, EventType::Id, args,
                                             EventType::Arity);
            event.Unpack(args);
            return verdict;
        }
    }

    /**
     * Raise @a event; the world does not consult the answer.
     *
     * @a where is a Map*, a WorldObject* or a Context -- whatever names the
     * state the event belongs to.
     */
    template <class Where, class Ev>
    void Notify(Where where, Ev&& event)
    {
        if (!detail::g_scriptsEnabled)
        {
            return;
        }

        detail::Emit(detail::ToContext(where), std::forward<Ev>(event));
    }

    /**
     * Raise @a event and honour the answer: Verdict::Cancel means the scripts
     * refused the action and the caller must not proceed.
     *
     * Only an event that declares itself cancellable may be asked; anything
     * else is a compile error rather than a silently ignored return value.
     */
    template <class Where, class Ev>
    Verdict Ask(Where where, Ev&& event)
    {
        static_assert(std::decay<Ev>::type::Cancellable,
                      "this event cannot be cancelled -- use Notify()");

        if (!detail::g_scriptsEnabled)
        {
            return Verdict::Continue;
        }

        return detail::Emit(detail::ToContext(where), std::forward<Ev>(event));
    }

    /**
     * Offer @a event to the engines and report whether one of them produced
     * the behaviour, in which case the core skips its own default.
     *
     * This is the third meaning that used to hide inside a single bool, and
     * splitting it out is what stops a Lua gossip script from silently
     * disabling the C++ one for the same NPC: claiming the behaviour and
     * refusing the action are now different answers to different questions,
     * and "no other engine gets a turn" is no longer a side effect of the
     * order the #ifdefs happened to nest in.
     *
     * @return true when an engine handled it; false to run the core default.
     */
    template <class Where, class Ev>
    bool Offer(Where where, Ev&& event)
    {
        static_assert(std::decay<Ev>::type::Claimable,
                      "this event cannot be claimed -- use Notify() or Ask()");

        if (!detail::g_scriptsEnabled)
        {
            return false;
        }

        return detail::Emit(detail::ToContext(where), std::forward<Ev>(event))
                   == Verdict::Handled;
    }

    /**
     * Auction a role: ask every engine what it offers, let the best one build.
     *
     * Returns nullptr when nobody wanted it or nobody could build, and the
     * caller falls back to whatever it did before -- for a creature, the AI
     * registry. Ownership passes to the caller.
     *
     * These are not templates: there are exactly three roles, and naming them
     * is more honest than erasing the type of what gets built.
     */
    CreatureAI*   ClaimCreatureAI(Creature* creature);
    GameObjectAI* ClaimGameObjectAI(GameObject* go);
    InstanceData* ClaimInstanceData(Map* map);

    /**
     * Engine lifetime, which is not an event and never becomes one.
     *
     * An engine having its own timers pumped, or being told a state has gone,
     * is housekeeping rather than something that happened in the world. Put
     * through the dispatch table it would be visible to every other engine,
     * which is exactly wrong.
     */
    void Tick(Context const& ctx, uint32 diff);
    void RetireState(Context const& ctx);
}

#endif //MANGOS_SCRIPT_HOST_H
