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

#include "dbscripts/DbScriptEngine.h"
#include "eventai/EventAiEngine.h"
#ifdef ENABLE_SD3
#include "sd3/Sd3Engine.h"
#endif

// Complete types, not forward declarations: the auction upcasts Creature and
// GameObject to WorldObject, and with multiple inheritance in the hierarchy an
// upcast can adjust the pointer. A reinterpret_cast here would compile and be
// silently wrong.
#include "BattleGround/BattleGround.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "Object.h"
#include "QuestDef.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace scripting
{
    namespace detail
    {
        // Is there any engine at all in this build?
        //
        // True while MakeHostState pushes at least one. With none built, every
        // emit site in the world folds to this single test and never becomes a
        // call. Whether a *state* exists for a given map stays a runtime
        // question for the engine, which answers Continue when it has none.
        bool g_scriptsEnabled = true;
    }

    namespace
    {
        /// Everything the seam hides: one object, one owner, one place.
        struct HostState
        {
            std::vector<std::unique_ptr<IEngine>> engines;
        };

        /**
         * Build the engine list.
         *
         * One line per engine, and that is the whole contract for adding one:
         * push it here, and make sure g_scriptsEnabled is true.
         *
         * ORDER IS PRECEDENCE for events. The auction sorts by bid and ignores
         * this order except to break ties, but the dispatch chain stops at the
         * first Cancel or Handled, so an engine listed earlier can end the
         * event before a later one sees it. Two events have more than one
         * listener today -- a gameobject being used and an event id being
         * raised -- and on both of them the world used to ask SD3 first and
         * fall back to the DB scripts only when SD3 declined. That is what
         * this order preserves; it is not alphabetical and not arbitrary.
         *
         * EventAI is last and it does not matter where it goes: it subscribes
         * to no event at all and only ever competes at the auction, where its
         * bid and not its position decides.
         */
        HostState MakeHostState()
        {
            HostState state;

#ifdef ENABLE_SD3
            // The one build-time question left in the seam: whether a back end
            // was compiled in at all. It is not an #ifdef about behaviour --
            // precedence between the engines is decided by bids and by the
            // order below, not by which #ifdef nests outermost.
            state.engines.push_back(
                std::unique_ptr<IEngine>(new Sd3Engine()));
#endif
            state.engines.push_back(
                std::unique_ptr<IEngine>(new DbScriptEngine()));
            state.engines.push_back(
                std::unique_ptr<IEngine>(new EventAiEngine()));

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

    // Guild and Group both carry a plain numeric id, so their handles are
    // exact. Quest and BattleGround are keyed by the id the world already uses
    // to look them up, for the same reason. Channel deliberately has no
    // HandleOf: see the note on Domain::Channel.
    Handle HandleOf(Guild const* guild)
    {
        return guild ? HandleOf(Domain::Guild,
                                const_cast<Guild*>(guild)->GetId())
                     : Handle{ 0, Domain::None };
    }

    Handle HandleOf(Group const* group)
    {
        return group ? HandleOf(Domain::Group, group->GetId())
                     : Handle{ 0, Domain::None };
    }

    Handle HandleOf(Quest const* quest)
    {
        return quest ? HandleOf(Domain::Quest, quest->GetQuestId())
                     : Handle{ 0, Domain::None };
    }

    Handle HandleOf(BattleGround const* bg)
    {
        // GetInstanceID, not GetClientInstanceID: the latter is the number the
        // client is shown ("Warsong Gulch 3") and is not unique across
        // battleground types.
        return bg ? HandleOf(Domain::BattleGround,
                             const_cast<BattleGround*>(bg)->GetInstanceID())
                  : Handle{ 0, Domain::None };
    }

    namespace
    {
        /**
         * Run the auction and let the winners build, best offer first.
         *
         * Two phases, and the split is the point: every engine is asked what
         * it offers before any of them builds, so a loser never constructs an
         * object that is then destroyed. A winner that returns nullptr has
         * declined after all, and the next best offer gets its turn -- SD3
         * bids on having a script bound to the entry, and that script's own
         * GetAI may still say no.
         */
        template <class Product, class Build>
        Product* Auction(Context const& ctx, RoleId role, Ref subject,
                         Build build)
        {
            if (!detail::g_scriptsEnabled || ctx.scope == Context::Scope::None)
            {
                return nullptr;
            }

            std::vector<std::pair<int, IEngine*>> offers;
            for (std::unique_ptr<IEngine> const& engine : State().engines)
            {
                int const offer = engine->Bid(ctx, role, subject);
                if (offer > NoBid)
                {
                    offers.push_back(std::make_pair(offer, engine.get()));
                }
            }

            // Highest offer first; stable, so an equal offer leaves the
            // configured engine order deciding rather than the sort.
            std::stable_sort(offers.begin(), offers.end(),
                [](std::pair<int, IEngine*> const& a,
                   std::pair<int, IEngine*> const& b)
                {
                    return a.first > b.first;
                });

            for (std::pair<int, IEngine*> const& offer : offers)
            {
                if (Product* product = build(offer.second))
                {
                    return product;
                }
            }

            return nullptr;
        }
    }

    // The state a ROLE belongs to is the map the object is on, which is not
    // the same test an event uses. An event for an object outside the world is
    // dropped, deliberately -- but a role is settled while the object is being
    // put into the world, and for a game object it is settled BEFORE: both
    // GameObject::LoadFromDB and the linked-object path call AIM_Initialize()
    // and only then Map::Add(). Asking for IsInWorld() here would have made
    // the game-object auction unreachable and quietly handed every scripted
    // object back to nothing.
    //
    // GetMap() is safe at every one of these call sites because SetMap()
    // happens during Create(), long before an AI is chosen.

    CreatureAI* ClaimCreatureAI(Creature* creature)
    {
        WorldObject const* subject = creature;
        Context const ctx = creature ? ContextOf(creature->GetMap())
                                     : Context{ Context::Scope::None, nullptr };

        return Auction<CreatureAI>(ctx, RoleId::CreatureAI, RefOf(subject),
            [&ctx, creature](IEngine* engine)
            {
                return engine->MakeCreatureAI(ctx, creature);
            });
    }

    GameObjectAI* ClaimGameObjectAI(GameObject* go)
    {
        WorldObject const* subject = go;
        Context const ctx = go ? ContextOf(go->GetMap())
                               : Context{ Context::Scope::None, nullptr };

        return Auction<GameObjectAI>(ctx, RoleId::GameObjectAI, RefOf(subject),
            [&ctx, go](IEngine* engine)
            {
                return engine->MakeGameObjectAI(ctx, go);
            });
    }

    InstanceData* ClaimInstanceData(Map* map)
    {
        Context const ctx = detail::ToContext(map);

        return Auction<InstanceData>(ctx, RoleId::InstanceData, Ref{ 0 },
            [&ctx, map](IEngine* engine)
            {
                return engine->MakeInstanceData(ctx, map);
            });
    }

    void Tick(Context const& ctx, uint32 diff)
    {
        if (!detail::g_scriptsEnabled || ctx.scope == Context::Scope::None)
        {
            return;
        }

        for (std::unique_ptr<IEngine> const& engine : State().engines)
        {
            engine->Tick(ctx, diff);
        }
    }

    void LoadData(LoadPhase phase)
    {
        if (!detail::g_scriptsEnabled)
        {
            return;
        }

        // No Context: loading is not something that happens on a map. It runs
        // once, on the world thread, before any map exists.
        for (std::unique_ptr<IEngine> const& engine : State().engines)
        {
            engine->LoadData(phase);
        }
    }

    bool ReloadData(char const* table)
    {
        if (!detail::g_scriptsEnabled || !table)
        {
            return false;
        }

        // First match wins and the rest are not asked. Two engines claiming
        // one table name would be a collision worth finding, but the chain
        // cannot report it, so this stops at the owner and says so.
        for (std::unique_ptr<IEngine> const& engine : State().engines)
        {
            if (engine->ReloadData(table))
            {
                return true;
            }
        }

        return false;
    }

    void RetireState(Context const& ctx)
    {
        if (!detail::g_scriptsEnabled || ctx.scope == Context::Scope::None)
        {
            return;
        }

        for (std::unique_ptr<IEngine> const& engine : State().engines)
        {
            engine->RetireState(ctx);
        }
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

        // Per-thread, and it must stay that way. Maps update in parallel, so a
        // shared counter would let one map's dispatch expire a borrow another
        // map is still legitimately holding. Starts at 1 so a default-built
        // Borrow, whose epoch is 0, is never mistaken for a live one.
        static thread_local uint32 s_epoch = 1;
        static thread_local uint32 s_depth = 0;

        uint32 CurrentEpoch()
        {
            return s_epoch;
        }

        bool IsBorrowLive(Borrow const& borrow)
        {
            return borrow.target != nullptr && borrow.epoch == s_epoch;
        }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count)
        {
            if (ctx.scope == Context::Scope::None)
            {
                return Verdict::Continue;
            }

            // Everything lent during this call dies with it. An engine that
            // squirrels a Borrow away and reads it on a later tick finds an
            // epoch that no longer matches, which is a reportable script error
            // rather than a read of freed memory. The pointer is still a
            // pointer; what changed is that using it late is *detected*.
            //
            // The depth counter is not decoration. A script handling an event
            // can cause the world to raise another one, and a plain bump on
            // return would expire the OUTER call's borrows while its frame is
            // still live -- turning a correct engine into a broken one. Only
            // the outermost dispatch ends the epoch. The cost is that a borrow
            // issued by a nested call stays valid until the outer one returns,
            // which is permissive rather than wrong, and rare either way.
            struct EpochGuard
            {
                EpochGuard() { ++s_depth; }
                ~EpochGuard()
                {
                    if (--s_depth == 0)
                    {
                        ++s_epoch;
                    }
                }
            } guard;

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
