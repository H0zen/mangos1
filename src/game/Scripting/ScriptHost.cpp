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

// Complete types, not forward declarations: the auction upcasts Creature and
// GameObject to WorldObject, and with multiple inheritance in the hierarchy an
// upcast can adjust the pointer. A reinterpret_cast here would compile and be
// silently wrong.
#include "Creature.h"
#include "GameObject.h"
#include "Object.h"

#ifdef ENABLE_ELUNA
#include "ElunaEngine.h"
#endif /* ENABLE_ELUNA */

#include <algorithm>
#include <memory>
#include <utility>
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

    CreatureAI* ClaimCreatureAI(Creature* creature)
    {
        WorldObject const* subject = creature;
        Context const ctx = detail::ToContext(subject);

        return Auction<CreatureAI>(ctx, RoleId::CreatureAI, RefOf(subject),
            [&ctx, creature](IEngine* engine)
            {
                return engine->MakeCreatureAI(ctx, creature);
            });
    }

    GameObjectAI* ClaimGameObjectAI(GameObject* go)
    {
        WorldObject const* subject = go;
        Context const ctx = detail::ToContext(subject);

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
