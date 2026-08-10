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

#include "Sd3Engine.h"

#ifdef ENABLE_SD3

#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Log.h"
#include "QuestDef.h"
#include "sd3/ScriptBindings.h"
#include "SpellAuras.h"
#include "Spell.h"

#include "system/ScriptDevMgr.h"

#include <cstring>

namespace scripting
{
    namespace
    {
        /**
         * The subjects a hook needs, resolved once at the top of a case.
         *
         * Every SD3 entry point takes pointers, and the seam carries
         * identities, so every case begins with the same three or four
         * lookups. Doing them through one small struct keeps each case to the
         * shape of the call it is adapting instead of five lines of casting.
         */
        Player* PlayerOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetPlayer(ObjectGuid(ref.guid));
        }

        Creature* CreatureOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetAnyTypeCreature(ObjectGuid(ref.guid));
        }

        GameObject* GameObjectOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetGameObject(ObjectGuid(ref.guid));
        }

        Unit* UnitOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetUnit(ObjectGuid(ref.guid));
        }

        WorldObject* ObjectOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetWorldObject(ObjectGuid(ref.guid));
        }

        /**
         * An item, which is the one subject a map cannot find.
         *
         * Items live in a player's inventory rather than in the world, so
         * there is no map-wide store to ask. Every item event carries the
         * player it belongs to for exactly this reason, and the search is that
         * player's bags.
         */
        Item* ItemOf(Player* player, Ref ref)
        {
            return (player && !ref.IsEmpty())
                       ? player->GetItemByGuid(ObjectGuid(ref.guid))
                       : nullptr;
        }

        Quest const* QuestOf(Handle handle)
        {
            return handle.domain == Domain::Quest
                       ? sObjectMgr.GetQuestTemplate(
                             static_cast<uint32>(handle.id))
                       : nullptr;
        }

        /// A borrowed pointer, refused unless it is still inside its call.
        template <class T>
        T* Borrowed(Borrow const& borrow, Domain domain)
        {
            if (borrow.domain != domain || !detail::IsBorrowLive(borrow))
            {
                return nullptr;
            }

            return static_cast<T*>(borrow.target);
        }

        /// Verdict for a hook whose SD3 answer means "I produced this".
        Verdict Claimed(bool handled)
        {
            return handled ? Verdict::Handled : Verdict::Continue;
        }
    }

    int Sd3Engine::Bid(Context const& ctx, RoleId role, Ref subject)
    {
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return NoBid;
        }

        // Whether a script is BOUND is a lookup; whether it will build is not,
        // and the two are deliberately not the same question. Bidding on the
        // binding and letting Make* come back empty is what the auction's two
        // phases are for.
        switch (role)
        {
            case RoleId::CreatureAI:
            {
                Creature const* creature = CreatureOn(ctx, subject);
                return (creature && creature->GetScriptId()) ? BidStrong
                                                             : NoBid;
            }

            case RoleId::GameObjectAI:
            {
                // From the guid, and not by looking the object up: a game
                // object has its AI chosen before Map::Add() puts it in the
                // store, so there is nothing to find. What GameObject::
                // GetScriptId() reads is the guid low and the entry, and both
                // are in the guid -- for a game object the entry in the guid
                // is the entry it keeps, unlike a creature, which a heroic
                // template or a game event can re-enter after creation.
                //
                // If the core ever changes that rule this bid goes stale, and
                // harmlessly: MakeGameObjectAI asks the object itself, so a
                // bid that is too low loses a role nobody else wants and one
                // that is too high builds nothing.
                ObjectGuid const guid(subject.guid);
                if (guid.IsEmpty())
                {
                    return NoBid;
                }

                uint32 const byGuid = sScriptBindings.GetBoundScriptId(
                    SCRIPTED_GAMEOBJECT, -int32(guid.GetCounter()));

                return (byGuid || sScriptBindings.GetBoundScriptId(
                            SCRIPTED_GAMEOBJECT, guid.GetEntry()))
                           ? BidStrong
                           : NoBid;
            }

            case RoleId::InstanceData:
            {
                // No subject: the map itself is what carries the script name.
                return ctx.map->GetScriptId() ? BidStrong : NoBid;
            }
        }

        return NoBid;
    }

    CreatureAI* Sd3Engine::MakeCreatureAI(Context const& ctx,
                                          Creature* creature)
    {
        (void)ctx;
        return creature ? SD3::GetCreatureAI(creature) : nullptr;
    }

    GameObjectAI* Sd3Engine::MakeGameObjectAI(Context const& ctx,
                                              GameObject* go)
    {
        (void)ctx;
        return go ? SD3::GetGameObjectAI(go) : nullptr;
    }

    InstanceData* Sd3Engine::MakeInstanceData(Context const& ctx, Map* map)
    {
        (void)ctx;
        return map ? SD3::CreateInstanceData(map) : nullptr;
    }

    void Sd3Engine::LoadData(LoadPhase phase)
    {
        switch (phase)
        {
            case LoadPhase::Bindings:
                // script_binding maps a ScriptName to a script id, and that is
                // all this engine's own binding is. It needs nothing else
                // loaded, and everything that asks GetBoundScriptId() -- the
                // bids, the spell and map-event lookups -- needs it, so it is
                // the first thing that happens.
                sLog.outString("Loading all script bindings...");
                sScriptBindings.LoadScriptBinding();
                break;

            case LoadPhase::Final:
                // Registering the scripts themselves. Free first: this is the
                // same pair of calls the .loadscripts command makes, so the
                // second time round there is a previous registry to drop.
                sLog.outString("Registering the C++ scripts...");
                SD3::FreeScriptLibrary();
                SD3::InitScriptLibrary();
                sLog.outString("%s", SD3::GetScriptLibraryVersion());
                break;

            default:
                break;
        }
    }

    bool Sd3Engine::ReloadData(char const* table)
    {
        if (std::strcmp(table, "script_binding") == 0)
        {
            sScriptBindings.LoadScriptBinding();
            return true;
        }

        return false;
    }

    Verdict Sd3Engine::Dispatch(Context const& ctx, EventId id, Arg* args,
                                std::size_t count)
    {
        // Every subject SD3 takes is looked up on a map. An event raised in
        // the global scope names nothing this engine can act on.
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return Verdict::Continue;
        }

        switch (id)
        {
            // -- gossip. Hello opens a menu, select chooses a line. SD3 splits
            //    select into coded and uncoded entry points; the seam does not,
            //    because it is one thing happening with the text field empty,
            //    so the empty string is what decides which one to call.

            case EventId::GossipCreatureHello:
            {
                MANGOS_ASSERT(count == GossipCreatureHello::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (!player || !creature)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::GossipHello(player, creature));
            }

            case EventId::GossipGameobjectHello:
            {
                MANGOS_ASSERT(count == GossipGameobjectHello::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (!player || !go)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::GOGossipHello(player, go));
            }

            case EventId::GossipItemHello:
            {
                MANGOS_ASSERT(count == GossipItemHello::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Item* item = ItemOf(player, args[1].AsEntity());
                if (!player || !item)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::ItemGossipHello(player, item));
            }

            case EventId::GossipCreatureSelect:
            {
                MANGOS_ASSERT(count == GossipCreatureSelect::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (!player || !creature)
                {
                    return Verdict::Continue;
                }

                uint32 const sender = static_cast<uint32>(args[2].AsNumber());
                uint32 const action = static_cast<uint32>(args[3].AsNumber());
                std::string const& code = args[4].AsText();

                return Claimed(code.empty()
                    ? SD3::GossipSelect(player, creature, sender, action)
                    : SD3::GossipSelectWithCode(player, creature, sender,
                                                action, code.c_str()));
            }

            case EventId::GossipGameobjectSelect:
            {
                MANGOS_ASSERT(count == GossipGameobjectSelect::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (!player || !go)
                {
                    return Verdict::Continue;
                }

                uint32 const sender = static_cast<uint32>(args[2].AsNumber());
                uint32 const action = static_cast<uint32>(args[3].AsNumber());
                std::string const& code = args[4].AsText();

                return Claimed(code.empty()
                    ? SD3::GOGossipSelect(player, go, sender, action)
                    : SD3::GOGossipSelectWithCode(player, go, sender, action,
                                                  code.c_str()));
            }

            case EventId::GossipItemSelect:
            {
                MANGOS_ASSERT(count == GossipItemSelect::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Item* item = ItemOf(player, args[1].AsEntity());
                if (!player || !item)
                {
                    return Verdict::Continue;
                }

                uint32 const sender = static_cast<uint32>(args[2].AsNumber());
                uint32 const action = static_cast<uint32>(args[3].AsNumber());
                std::string const& code = args[4].AsText();

                return Claimed(code.empty()
                    ? SD3::ItemGossipSelect(player, item, sender, action)
                    : SD3::ItemGossipSelectWithCode(player, item, sender,
                                                    action, code.c_str()));
            }

            // -- quests

            case EventId::CreatureQuestAccept:
            {
                MANGOS_ASSERT(count == CreatureQuestAccept::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (!player || !creature || !quest)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::QuestAccept(player, creature, quest));
            }

            case EventId::GameobjectQuestAccept:
            {
                MANGOS_ASSERT(count == GameobjectQuestAccept::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (!player || !go || !quest)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::GOQuestAccept(player, go, quest));
            }

            case EventId::ItemQuestAccept:
            {
                MANGOS_ASSERT(count == ItemQuestAccept::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Item* item = ItemOf(player, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (!player || !item || !quest)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::ItemQuestAccept(player, item, quest));
            }

            case EventId::CreatureQuestReward:
            {
                MANGOS_ASSERT(count == CreatureQuestReward::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (!player || !creature || !quest)
                {
                    return Verdict::Continue;
                }

                // The chosen reward is in args[3] and SD3 does not take it.
                // That is the engine's business, not a gap in the event: the
                // world knows which reward was picked whether or not any
                // particular back end asks.
                return Claimed(SD3::QuestRewarded(player, creature, quest));
            }

            case EventId::GameobjectQuestReward:
            {
                MANGOS_ASSERT(count == GameobjectQuestReward::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                Quest const* quest = QuestOf(args[2].AsNamed());
                if (!player || !go || !quest)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::GOQuestRewarded(player, go, quest));
            }

            // -- dialog status. The answer is a value, so it goes back in the
            //    payload slot; the Verdict says only whether a script gave
            //    one. SD3 answers DIALOG_STATUS_UNDEFINED both when no script
            //    is bound and when a bound script has nothing to say, and
            //    those two have always been indistinguishable here.

            case EventId::CreatureDialogStatus:
            {
                MANGOS_ASSERT(count == CreatureDialogStatus::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (!player || !creature)
                {
                    return Verdict::Continue;
                }

                uint32 const status =
                    SD3::GetNPCDialogStatus(player, creature);
                if (status == DIALOG_STATUS_UNDEFINED)
                {
                    return Verdict::Continue;
                }

                args[2] = Arg::FromNumber(status);
                return Verdict::Handled;
            }

            case EventId::GameobjectDialogStatus:
            {
                MANGOS_ASSERT(count == GameobjectDialogStatus::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (!player || !go)
                {
                    return Verdict::Continue;
                }

                uint32 const status = SD3::GetGODialogStatus(player, go);
                if (status == DIALOG_STATUS_UNDEFINED)
                {
                    return Verdict::Continue;
                }

                args[2] = Arg::FromNumber(status);
                return Verdict::Handled;
            }

            // -- using things

            case EventId::GameobjectUse:
            {
                MANGOS_ASSERT(count == GameobjectUse::Arity);

                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (!go)
                {
                    return Verdict::Continue;
                }

                // SD3 has two entry points for this and they run the same
                // script method; which one applies is a question about the
                // user, answered here rather than by the world raising two
                // events for one moment.
                if (Player* player = PlayerOn(ctx, args[0].AsEntity()))
                {
                    return Claimed(SD3::GOUse(player, go));
                }

                return Verdict::Continue;
            }

            case EventId::GameobjectTrapSprung:
            {
                MANGOS_ASSERT(count == GameobjectTrapSprung::Arity);

                Unit* unit = UnitOn(ctx, args[0].AsEntity());
                GameObject* go = GameObjectOn(ctx, args[1].AsEntity());
                if (!unit || !go)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::GOUse(unit, go));
            }

            case EventId::ItemUse:
            {
                MANGOS_ASSERT(count == ItemUse::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Item* item = ItemOf(player, args[1].AsEntity());
                SpellCastTargets const* targets =
                    Borrowed<SpellCastTargets const>(args[2].AsLent(),
                                                     Domain::CastTargets);
                if (!player || !item || !targets)
                {
                    return Verdict::Continue;
                }

                // The opposite polarity to every claim above, and the reason
                // this event is cancellable rather than claimable: a script
                // returning true here has BLOCKED the item's own spell, which
                // is a refusal, not a substitute behaviour.
                return SD3::ItemUse(player, item, *targets) ? Verdict::Cancel
                                                            : Verdict::Continue;
            }

            case EventId::ServerEventTrigger:
            {
                MANGOS_ASSERT(count == ServerEventTrigger::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Handle const handle = args[1].AsNamed();
                if (!player || handle.domain != Domain::AreaTrigger)
                {
                    return Verdict::Continue;
                }

                AreaTriggerEntry const* entry = sAreaTriggerStore.LookupEntry(
                    static_cast<uint32>(handle.id));
                if (!entry)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::AreaTrigger(player, entry));
            }

            case EventId::CoreNpcSpellClick:
            {
                MANGOS_ASSERT(count == CoreNpcSpellClick::Arity);

                Player* player = PlayerOn(ctx, args[0].AsEntity());
                Creature* creature = CreatureOn(ctx, args[1].AsEntity());
                if (!player || !creature)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::NpcSpellClick(
                    player, creature,
                    static_cast<uint32>(args[2].AsNumber())));
            }

            case EventId::ServerEventRaised:
            {
                MANGOS_ASSERT(count == ServerEventRaised::Arity);

                // Source and target may be a vessel, which is a game object
                // that no object store holds -- Map::GetTransport is what
                // makes one reachable by guid at all.
                WorldObject* source = ObjectOn(ctx, args[0].AsEntity());
                WorldObject* target = ObjectOn(ctx, args[1].AsEntity());
                if (!source)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::ProcessEvent(
                    static_cast<uint32>(args[2].AsNumber()), source, target,
                    args[3].AsFlag()));
            }

            // -- spell effects

            case EventId::CoreEffectDummy:
            {
                MANGOS_ASSERT(count == CoreEffectDummy::Arity);

                Unit* caster = UnitOn(ctx, args[0].AsEntity());
                if (!caster)
                {
                    return Verdict::Continue;
                }

                uint32 const spellId = static_cast<uint32>(args[1].AsNumber());
                SpellEffectIndex const effIndex =
                    SpellEffectIndex(args[2].AsNumber());
                ObjectGuid const targetGuid(args[3].AsEntity().guid);
                ObjectGuid const originalCaster(args[4].AsEntity().guid);

                // The script is bound to the spell, so which of the three
                // entry points applies is decided by what the effect landed
                // on. The guid already says which kind of thing that is.
                if (targetGuid.IsItem())
                {
                    Item* item = ItemOf(caster->ToPlayer(),
                                        args[3].AsEntity());
                    return item ? Claimed(SD3::EffectDummyItem(
                                      caster, spellId, effIndex, item,
                                      originalCaster))
                                : Verdict::Continue;
                }

                if (targetGuid.IsGameObject())
                {
                    GameObject* go = GameObjectOn(ctx, args[3].AsEntity());
                    return go ? Claimed(SD3::EffectDummyGameObject(
                                    caster, spellId, effIndex, go,
                                    originalCaster))
                              : Verdict::Continue;
                }

                Unit* target = UnitOn(ctx, args[3].AsEntity());
                return target ? Claimed(SD3::EffectDummyUnit(
                                    caster, spellId, effIndex, target,
                                    originalCaster))
                              : Verdict::Continue;
            }

            case EventId::CoreEffectScriptEffect:
            {
                MANGOS_ASSERT(count == CoreEffectScriptEffect::Arity);

                Unit* caster = UnitOn(ctx, args[0].AsEntity());
                Unit* target = UnitOn(ctx, args[3].AsEntity());
                if (!caster || !target)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::EffectScriptEffectUnit(
                    caster, static_cast<uint32>(args[1].AsNumber()),
                    SpellEffectIndex(args[2].AsNumber()), target,
                    ObjectGuid(args[4].AsEntity().guid)));
            }

            case EventId::CoreAuraDummy:
            {
                MANGOS_ASSERT(count == CoreAuraDummy::Arity);

                Aura const* aura =
                    Borrowed<Aura const>(args[0].AsLent(), Domain::Aura);
                if (!aura)
                {
                    return Verdict::Continue;
                }

                return Claimed(SD3::AuraDummy(aura, args[1].AsFlag()));
            }

            default:
                return Verdict::Continue;
        }
    }
}

#endif //ENABLE_SD3
