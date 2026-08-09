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
#include "ElunaConfig.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "LuaEngine.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Spell.h"
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

        SpellCastTargets const* BorrowedTargets(Borrow borrow)
        {
            if (borrow.domain != Domain::CastTargets
                || !detail::IsBorrowLive(borrow))
            {
                return nullptr;
            }

            return static_cast<SpellCastTargets const*>(borrow.target);
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

        GameObject* GameObjectOn(Context const& ctx, Ref ref)
        {
            if (ref.IsEmpty() || ctx.scope != Context::Scope::Map || !ctx.map)
            {
                return nullptr;
            }

            return ctx.map->GetGameObject(ObjectGuid(ref.guid));
        }

        /// An item is only ever reachable through the player who holds it.
        Item* ItemOf(Player* owner, Ref ref)
        {
            return (owner && !ref.IsEmpty())
                       ? owner->GetItemByGuid(ObjectGuid(ref.guid))
                       : nullptr;
        }

        Quest const* QuestOf(Handle handle)
        {
            return handle.domain == Domain::Quest
                       ? sObjectMgr.GetQuestTemplate(
                             static_cast<uint32>(handle.id))
                       : nullptr;
        }

        AreaTriggerEntry const* TriggerOf(Handle handle)
        {
            return handle.domain == Domain::AreaTrigger
                       ? sAreaTriggerStore.LookupEntry(
                             static_cast<uint32>(handle.id))
                       : nullptr;
        }

        /// Every dummy-effect arm shares this; only the target type differs.
        WorldObject* CasterOn(Context const& ctx, Ref ref)
        {
            if (ref.IsEmpty() || ctx.scope != Context::Scope::Map || !ctx.map)
            {
                return nullptr;
            }

            return ctx.map->GetWorldObject(ObjectGuid(ref.guid));
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

            // -- gossip. Claimed, not cancelled: a script that answers has
            //    produced the menu itself, so the core skips its default one.

            case EventId::GossipCreatureHello:
            {
                MANGOS_ASSERT(count == GossipCreatureHello::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (player && creature && engine->OnGossipHello(player, creature))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::GossipGameobjectHello:
            {
                MANGOS_ASSERT(count == GossipGameobjectHello::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (player && go && engine->OnGossipHello(player, go))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::GossipCreatureSelect:
            {
                MANGOS_ASSERT(count == GossipCreatureSelect::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (!player || !creature)
                {
                    return Verdict::Continue;
                }

                // Eluna splits coded and uncoded selection into two hooks; the
                // seam carries one event whose code is simply empty.
                std::string& code = args[4].AsText();
                uint32 const sender = static_cast<uint32>(args[2].AsNumber());
                uint32 const action = static_cast<uint32>(args[3].AsNumber());

                bool const handled = code.empty()
                    ? engine->OnGossipSelect(player, creature, sender, action)
                    : engine->OnGossipSelectCode(player, creature, sender,
                                                 action, code.c_str());

                return handled ? Verdict::Handled : Verdict::Continue;
            }

            case EventId::GossipGameobjectSelect:
            {
                MANGOS_ASSERT(count == GossipGameobjectSelect::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (!player || !go)
                {
                    return Verdict::Continue;
                }

                std::string& code = args[4].AsText();
                uint32 const sender = static_cast<uint32>(args[2].AsNumber());
                uint32 const action = static_cast<uint32>(args[3].AsNumber());

                bool const handled = code.empty()
                    ? engine->OnGossipSelect(player, go, sender, action)
                    : engine->OnGossipSelectCode(player, go, sender, action,
                                                 code.c_str());

                return handled ? Verdict::Handled : Verdict::Continue;
            }

            // -- quests

            case EventId::CreatureQuestAccept:
            {
                MANGOS_ASSERT(count == CreatureQuestAccept::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (player && creature && quest
                    && engine->OnQuestAccept(player, creature, quest))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::GameobjectQuestAccept:
            {
                MANGOS_ASSERT(count == GameobjectQuestAccept::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (player && go && quest
                    && engine->OnQuestAccept(player, go, quest))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::ItemQuestAccept:
            {
                MANGOS_ASSERT(count == ItemQuestAccept::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Item* item = ItemOf(player, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (player && item && quest
                    && engine->OnQuestAccept(player, item, quest))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::CreatureQuestReward:
            {
                MANGOS_ASSERT(count == CreatureQuestReward::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (player && creature && quest
                    && engine->OnQuestReward(player, creature, quest,
                           static_cast<uint32>(args[3].AsNumber())))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::GameobjectQuestReward:
            {
                MANGOS_ASSERT(count == GameobjectQuestReward::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (player && go && quest
                    && engine->OnQuestReward(player, go, quest,
                           static_cast<uint32>(args[3].AsNumber())))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            // -- dialog status. Eluna returns nothing here; it pushes the
            //    status into its own state, so this can only ever be Continue.

            case EventId::CreatureDialogStatus:
            {
                MANGOS_ASSERT(count == CreatureDialogStatus::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (player && creature)
                {
                    engine->GetDialogStatus(player, creature);
                }

                return Verdict::Continue;
            }

            case EventId::GameobjectDialogStatus:
            {
                MANGOS_ASSERT(count == GameobjectDialogStatus::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (player && go)
                {
                    engine->GetDialogStatus(player, go);
                }

                return Verdict::Continue;
            }

            // -- use

            case EventId::GameobjectUse:
            {
                MANGOS_ASSERT(count == GameobjectUse::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (player && go && engine->OnGameObjectUse(player, go))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            case EventId::ItemUse:
            {
                MANGOS_ASSERT(count == ItemUse::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                Item* item = ItemOf(player, args[1].AsEntity());
                SpellCastTargets const* targets =
                    BorrowedTargets(args[2].AsLent());
                if (!player || !item || !targets)
                {
                    return Verdict::Continue;
                }

                // Inverted, and deliberately so: Eluna::OnUse returns TRUE to
                // mean "go ahead and cast" and FALSE when a script blocked it.
                // That is the opposite polarity to OnGossipHello, which is why
                // this is a cancel and not a claim.
                return engine->OnUse(player, item, *targets)
                           ? Verdict::Continue : Verdict::Cancel;
            }

            case EventId::ServerEventTrigger:
            {
                MANGOS_ASSERT(count == ServerEventTrigger::Arity);

                Player* player = PlayerOf(args[0].AsEntity());
                AreaTriggerEntry const* trigger = TriggerOf(args[1].AsNamed());
                if (player && trigger
                    && engine->OnAreaTrigger(player, trigger))
                {
                    return Verdict::Handled;
                }

                return Verdict::Continue;
            }

            // -- dummy effects. One shape, three target types.

            case EventId::CreatureDummyEffect:
            case EventId::GameobjectDummyEffect:
            case EventId::ItemDummyEffect:
            {
                MANGOS_ASSERT(count == CreatureDummyEffect::Arity);

                WorldObject* caster = CasterOn(ctx, args[0].AsEntity());
                if (!caster)
                {
                    return Verdict::Continue;
                }

                uint32 const spellId = static_cast<uint32>(args[1].AsNumber());
                SpellEffectIndex const effIndex =
                    static_cast<SpellEffectIndex>(args[2].AsNumber());

                if (id == EventId::CreatureDummyEffect)
                {
                    if (Creature* target = CreatureOn(ctx, args[3].AsEntity()))
                    {
                        engine->OnDummyEffect(caster, spellId, effIndex, target);
                    }
                }
                else if (id == EventId::GameobjectDummyEffect)
                {
                    if (GameObject* target =
                            GameObjectOn(ctx, args[3].AsEntity()))
                    {
                        engine->OnDummyEffect(caster, spellId, effIndex, target);
                    }
                }
                else
                {
                    Player* owner = caster->ToPlayer();
                    if (Item* target = ItemOf(owner, args[3].AsEntity()))
                    {
                        engine->OnDummyEffect(caster, spellId, effIndex, target);
                    }
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

    void ElunaEngine::Tick(Context const& ctx, uint32 diff)
    {
        Eluna* engine = StateFor(ctx);
        if (!engine)
        {
            return;
        }

        // Compatibility mode collapses every map onto the world state, so
        // pumping per map would pump the one state once per map per tick.
        // The old call site guarded this the same way; the guard has to come
        // with it.
        if (ctx.scope == Context::Scope::Map
            && sElunaConfig->IsElunaCompatibilityMode())
        {
            return;
        }

        engine->UpdateEluna(diff);
    }

    void ElunaEngine::RetireState(Context const& ctx)
    {
        Eluna* engine = StateFor(ctx);
        if (engine && ctx.scope == Context::Scope::Map && ctx.map
            && ctx.map->Instanceable())
        {
            engine->FreeInstanceId(ctx.map->GetInstanceId());
        }
    }
}

#endif /* ENABLE_ELUNA */
