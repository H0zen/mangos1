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

#include "Channel.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
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

        Group* GroupOf(Handle handle)
        {
            return handle.domain == Domain::Group
                       ? sObjectMgr.GetGroupById(static_cast<uint32>(handle.id))
                       : nullptr;
        }

        Guild* GuildOf(Handle handle)
        {
            return handle.domain == Domain::Guild
                       ? sGuildMgr.GetGuildById(static_cast<uint32>(handle.id))
                       : nullptr;
        }

        /**
         * Unwrap a borrowed channel, but only while the borrow is live.
         *
         * This is the epoch doing its job. The pointer is still a raw pointer;
         * what the check buys is that a stale one -- kept past the dispatch
         * that lent it -- is refused here instead of being dereferenced.
         */
        Channel* BorrowedChannel(Borrow borrow)
        {
            if (borrow.domain != Domain::Channel
                || !detail::IsBorrowLive(borrow))
            {
                return nullptr;
            }

            return static_cast<Channel*>(borrow.target);
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

        Creature* CreatureOn(Context const& ctx, Ref ref)
        {
            if (ref.IsEmpty() || ctx.scope != Context::Scope::Map || !ctx.map)
            {
                return nullptr;
            }

            return ctx.map->GetCreature(ObjectGuid(ref.guid));
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

            case EventId::PlayerLevelChange:
            {
                MANGOS_ASSERT(count == PlayerLevelChange::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnLevelChanged(player,
                        static_cast<uint8>(args[1].AsNumber()));
                }

                return Verdict::Continue;
            }

            case EventId::PlayerTalentsChange:
            {
                MANGOS_ASSERT(count == PlayerTalentsChange::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnFreeTalentPointsChanged(player,
                        static_cast<uint32>(args[1].AsNumber()));
                }

                return Verdict::Continue;
            }

            case EventId::PlayerMoneyChange:
            {
                MANGOS_ASSERT(count == PlayerMoneyChange::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                if (!player)
                {
                    return Verdict::Continue;
                }

                int32 amount = static_cast<int32>(args[1].AsSigned());
                engine->OnMoneyChanged(player, amount);
                args[1] = Arg::FromSigned(amount);

                return Verdict::Continue;
            }

            case EventId::PlayerResurrect:
            {
                MANGOS_ASSERT(count == PlayerResurrect::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnResurrect(player);
                }

                return Verdict::Continue;
            }

            case EventId::PlayerDuelEnd:
            {
                MANGOS_ASSERT(count == PlayerDuelEnd::Arity);

                Player* winner = PlayerOf(args[0].AsEntity());
                Player* loser = PlayerOf(args[1].AsEntity());
                if (winner && loser)
                {
                    engine->OnDuelEnd(winner, loser,
                        static_cast<DuelCompleteType>(args[2].AsNumber()));
                }

                return Verdict::Continue;
            }

            case EventId::PlayerCanUseItem:
            {
                MANGOS_ASSERT(count == PlayerCanUseItem::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                if (!player)
                {
                    return Verdict::Continue;
                }

                // The answer is a value, not a veto: the hook returns an
                // InventoryResult, and EQUIP_ERR_OK means "no objection".
                InventoryResult const result = engine->OnCanUseItem(player,
                    static_cast<uint32>(args[1].AsNumber()));
                args[2] = Arg::FromNumber(static_cast<uint64>(result));

                return Verdict::Continue;
            }

            case EventId::PlayerBindToInstance:
            {
                MANGOS_ASSERT(count == PlayerBindToInstance::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnBindToInstance(player,
                        static_cast<Difficulty>(args[1].AsNumber()),
                        static_cast<uint32>(args[2].AsNumber()),
                        args[3].AsFlag());
                }

                return Verdict::Continue;
            }

            case EventId::PlayerWhisper:
            {
                MANGOS_ASSERT(count == PlayerWhisper::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Player* receiver = PlayerOf(args[4].AsEntity());
                if (!player || !receiver)
                {
                    return Verdict::Continue;
                }

                return engine->OnChat(player,
                           static_cast<uint32>(args[1].AsNumber()),
                           static_cast<uint32>(args[2].AsNumber()),
                           args[3].AsText(), receiver)
                           ? Verdict::Continue : Verdict::Cancel;
            }

            case EventId::PlayerGroupChat:
            {
                MANGOS_ASSERT(count == PlayerGroupChat::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Group* group = GroupOf(args[4].AsNamed());
                if (!player || !group)
                {
                    return Verdict::Continue;
                }

                return engine->OnChat(player,
                           static_cast<uint32>(args[1].AsNumber()),
                           static_cast<uint32>(args[2].AsNumber()),
                           args[3].AsText(), group)
                           ? Verdict::Continue : Verdict::Cancel;
            }

            case EventId::PlayerGuildChat:
            {
                MANGOS_ASSERT(count == PlayerGuildChat::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Guild* guild = GuildOf(args[4].AsNamed());
                if (!player || !guild)
                {
                    return Verdict::Continue;
                }

                return engine->OnChat(player,
                           static_cast<uint32>(args[1].AsNumber()),
                           static_cast<uint32>(args[2].AsNumber()),
                           args[3].AsText(), guild)
                           ? Verdict::Continue : Verdict::Cancel;
            }

            case EventId::PlayerChannelChat:
            {
                MANGOS_ASSERT(count == PlayerChannelChat::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Channel* channel = BorrowedChannel(args[4].AsLent());
                if (!player || !channel)
                {
                    return Verdict::Continue;
                }

                return engine->OnChat(player,
                           static_cast<uint32>(args[1].AsNumber()),
                           static_cast<uint32>(args[2].AsNumber()),
                           args[3].AsText(), channel)
                           ? Verdict::Continue : Verdict::Cancel;
            }

            case EventId::CreatureSummoned:
            {
                MANGOS_ASSERT(count == CreatureSummoned::Arity);

                Creature* summon = CreatureOn(ctx, args[0].AsEntity());
                Unit* summoner = UnitOn(ctx, args[1].AsEntity());
                if (summon && summoner)
                {
                    engine->OnSummoned(summon, summoner);
                }

                return Verdict::Continue;
            }

            case EventId::PlayerDuelStart:
            {
                MANGOS_ASSERT(count == PlayerDuelStart::Arity);

                Player* starter = PlayerOf(args[0].AsEntity());
                Player* challenger = PlayerOf(args[1].AsEntity());
                if (starter && challenger)
                {
                    engine->OnDuelStart(starter, challenger);
                }

                return Verdict::Continue;
            }

            case EventId::PlayerTalentsReset:
            {
                MANGOS_ASSERT(count == PlayerTalentsReset::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnTalentsReset(player, args[1].AsFlag());
                }

                return Verdict::Continue;
            }

            case EventId::PlayerUpdateZone:
            {
                MANGOS_ASSERT(count == PlayerUpdateZone::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnUpdateZone(player,
                        static_cast<uint32>(args[1].AsNumber()),
                        static_cast<uint32>(args[2].AsNumber()));
                }

                return Verdict::Continue;
            }

            case EventId::PlayerRepop:
            {
                MANGOS_ASSERT(count == PlayerRepop::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnRepop(player);
                }

                return Verdict::Continue;
            }

            case EventId::PlayerQuestAbandon:
            {
                MANGOS_ASSERT(count == PlayerQuestAbandon::Arity);

                if (Player* player = PlayerOf(args[0].AsEntity()))
                {
                    engine->OnQuestAbandon(player,
                        static_cast<uint32>(args[1].AsNumber()));
                }

                return Verdict::Continue;
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
