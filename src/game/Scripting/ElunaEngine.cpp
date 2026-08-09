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

#include "ElunaEngine.h"

#ifdef ENABLE_ELUNA

#include "LuaEngine.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"

namespace scripting
{
    namespace
    {
        /// Resolve the Lua state the way the old inline call sites did.
        Eluna* StateFor(Context const& ctx)
        {
            switch (ctx.scope)
            {
                case Context::Scope::Global:
                    return sWorld.GetEluna();
                case Context::Scope::Map:
                    return ctx.map ? ctx.map->GetEluna() : nullptr;
                default:
                    return nullptr;
            }
        }

        /**
         * Resolve a Ref back to the player it names.
         *
         * `inWorld = false` on purpose. The call sites this replaces held a
         * raw Player* and did not care whether the player was in the world; a
         * stricter lookup here would silently stop firing hooks that used to
         * fire, which is the one thing this step must not do.
         */
        Player* PlayerOf(Ref ref)
        {
            return ref.IsEmpty()
                       ? nullptr
                       : sObjectMgr.GetPlayer(ObjectGuid(ref.guid), false);
        }

        /// Units are resolved on the map that raised the event, never globally.
        Unit* UnitOn(Context const& ctx, Ref ref)
        {
            if (ref.IsEmpty() || ctx.scope != Context::Scope::Map || !ctx.map)
            {
                return nullptr;
            }

            return ctx.map->GetUnit(ObjectGuid(ref.guid));
        }
    }

    Verdict ElunaEngine::Dispatch(Context const& ctx, EventId id, Arg* args,
                                  std::size_t count)
    {
        Eluna* engine = StateFor(ctx);
        if (!engine)
        {
            return Verdict::Continue;
        }

        switch (id)
        {
            case EventId::PlayerSave:
            {
                MANGOS_ASSERT(count == PlayerSave::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnSave(player);
                }

                return Verdict::Continue;
            }

            case EventId::PlayerGiveXp:
            {
                MANGOS_ASSERT(count == PlayerGiveXp::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                if (!player)
                {
                    return Verdict::Continue;
                }

                // The hook takes uint32& and may lower or raise the award, so
                // the new value goes back into the slot it came from.
                uint32 amount = static_cast<uint32>(args[1].AsNumber());
                engine->OnGiveXP(player, amount, UnitOn(ctx, args[2].AsEntity()));
                args[1] = Arg::FromNumber(amount);

                return Verdict::Continue;
            }

            case EventId::PlayerChat:
            {
                MANGOS_ASSERT(count == PlayerChat::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                if (!player)
                {
                    return Verdict::Continue;
                }

                // The text is edited in place; only the refusal travels back.
                bool const allowed = engine->OnChat(
                    player,
                    static_cast<uint32>(args[1].AsNumber()),
                    static_cast<uint32>(args[2].AsNumber()),
                    args[3].AsText());

                return allowed ? Verdict::Continue : Verdict::Cancel;
            }

            default:
                // Not converted yet. Not delivered is correct; misdelivered
                // would not be.
                return Verdict::Continue;
        }
    }

    int ElunaEngine::Bid(Context const& ctx, RoleId role, Ref subject)
    {
        (void)subject;

        if (!StateFor(ctx))
        {
            return NoBid;
        }

        switch (role)
        {
            case RoleId::CreatureAI:
            case RoleId::InstanceData:
                // See the header: coarse on purpose, at the precedence Eluna
                // has always had over SD3.
                return BidNormal;

            default:
                // Game-object AI was never exposed in Eluna; ScriptMgr still
                // carries the "TODO - expose in Eluna" that says so.
                return NoBid;
        }
    }

    CreatureAI* ElunaEngine::MakeCreatureAI(Context const& ctx,
                                            Creature* creature)
    {
        Eluna* engine = StateFor(ctx);
        return engine ? engine->GetAI(creature) : nullptr;
    }

    InstanceData* ElunaEngine::MakeInstanceData(Context const& ctx, Map* map)
    {
        Eluna* engine = StateFor(ctx);
        return engine ? engine->GetInstanceData(map) : nullptr;
    }
}

#endif /* ENABLE_ELUNA */
