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

/**
 * @file ScriptMgr.cpp
 * @brief Script system manager implementation
 *
 * This file implements ScriptMgr which manages all game scripts:
 * - Creature AI scripts
 * - GameObject scripts
 * - Item scripts
 * - Area trigger scripts
 * - Spell scripts
 * - Quest scripts
 * - Instance scripts
 *
 * Scripts are loaded from script libraries and provide hooks for
 * customizing game behavior. The script manager routes events to
 * the appropriate script handlers.
 *
 * @see ScriptMgr for the manager class
 * @see ScriptedInstance for instance script base
 */



#include "ScriptMgr.h"
#include "ScriptHost.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "Policies/Singleton.h"
#include "Log.h"
#include "ProgressBar.h"
#include "ObjectMgr.h"
#include "WaypointManager.h"
#include "World.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Cell.h"
#include "CellImpl.h"
#include "SQLStorages.h"
#include "BattleGround/BattleGround.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "WaypointMovementGenerator.h"
#include "Mail.h"
#ifdef CLASSIC
#include "LFGMgr.h"
#endif /* CLASSIC */

/**
 * @brief Creates or retrieves scripted AI for a creature.
 *
 * @param pCreature The creature requiring AI.
 * @return CreatureAI* The scripted AI instance, or NULL when none is available.
 */
CreatureAI* ScriptMgr::GetCreatureAI(Creature* pCreature)
{
    // Exactly one engine can drive a creature, so this is an auction, not a
    // chain: every engine says what it offers, the best one builds, and a
    // bidder that declines after all drops the role to the next. It replaces
    // the old "whichever #ifdef nests outermost wins" without changing who
    // wins today -- a bound C++ script bids strongly and a template that only
    // names EventAI bids normally, which is the order this function had when
    // the two were an #ifdef and an AI-registry entry.
    return scripting::ClaimCreatureAI(pCreature);
}

/**
 * @brief Creates or retrieves scripted AI for a game object.
 *
 * @param pGo The game object requiring AI.
 * @return GameObjectAI* The scripted AI instance, or NULL when none is available.
 */
GameObjectAI* ScriptMgr::GetGameObjectAI(GameObject* pGo)
{
    return scripting::ClaimGameObjectAI(pGo);
}

/**
 * @brief Creates scripted instance data for a map.
 *
 * @param pMap The map requiring instance data.
 * @return InstanceData* The scripted instance data, or NULL when unavailable.
 */
InstanceData* ScriptMgr::CreateInstanceData(Map* pMap)
{
    // This function used to consult only SD3, even though the scripting engine
    // of the day implemented instance data too -- so that engine shipped
    // instance scripting nothing in this core ever reached. Going through the
    // auction is what keeps that from happening again to the next engine.
    return scripting::ClaimInstanceData(pMap);
}

/**
 * @brief Dispatches creature gossip hello hooks to scripting engines.
 *
 * @param pPlayer The player starting gossip.
 * @param pCreature The creature handling gossip.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnGossipHello(Player* pPlayer, Creature* pCreature)
{
    return scripting::Offer(pPlayer,
        scripting::GossipCreatureHello{ scripting::RefOf(pPlayer),
                                        scripting::RefOf(pCreature) });
}

/**
 * @brief Dispatches game object gossip hello hooks to scripting engines.
 *
 * @param pPlayer The player starting gossip.
 * @param pGameObject The game object handling gossip.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnGossipHello(Player* pPlayer, GameObject* pGameObject)
{
    return scripting::Offer(pPlayer,
        scripting::GossipGameobjectHello{ scripting::RefOf(pPlayer),
                                          scripting::RefOf(pGameObject) });
}

/**
 * @brief Dispatches item gossip hello hooks to scripting engines.
 *
 * @param pPlayer The player starting gossip.
 * @param pItem The item handling gossip.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnGossipHello(Player* pPlayer, Item* pItem)
{
    return scripting::Offer(pPlayer,
        scripting::GossipItemHello{ scripting::RefOf(pPlayer),
                                    scripting::RefOf(pItem) });
}

/**
 * @brief Dispatches creature gossip selection hooks to scripting engines.
 *
 * @param pPlayer The player selecting the option.
 * @param pCreature The gossip creature.
 * @param sender The menu sender identifier.
 * @param action The selected action identifier.
 * @param code Optional code text entered by the player.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnGossipSelect(Player* pPlayer, Creature* pCreature, uint32 sender, uint32 action, const char* code)
{
    // One event, not two: the engines split coded and uncoded
    // selection into separate hooks, but it is the same thing
    // happening with the text field empty.
    std::string selectCode(code ? code : "");
    return scripting::Offer(pPlayer,
            scripting::GossipCreatureSelect{ scripting::RefOf(pPlayer),
                                     scripting::RefOf(pCreature),
                                     sender, action, selectCode });
}

/**
 * @brief Dispatches game object gossip selection hooks to scripting engines.
 *
 * @param pPlayer The player selecting the option.
 * @param pGameObject The gossip game object.
 * @param sender The menu sender identifier.
 * @param action The selected action identifier.
 * @param code Optional code text entered by the player.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnGossipSelect(Player* pPlayer, GameObject* pGameObject, uint32 sender, uint32 action, const char* code)
{
    // One event, not two: the engines split coded and uncoded
    // selection into separate hooks, but it is the same thing
    // happening with the text field empty.
    std::string selectCode(code ? code : "");
    return scripting::Offer(pPlayer,
            scripting::GossipGameobjectSelect{ scripting::RefOf(pPlayer),
                                     scripting::RefOf(pGameObject),
                                     sender, action, selectCode });
}

/**
 * @brief Dispatches item gossip selection hooks to scripting engines.
 *
 * @param pPlayer The player selecting the option.
 * @param pItem The gossip item.
 * @param sender The menu sender identifier.
 * @param action The selected action identifier.
 * @param code Optional code text entered by the player.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnGossipSelect(Player* pPlayer, Item* pItem, uint32 sender, uint32 action, const char* code)
{
    // One event, not two: the engines split coded and uncoded
    // selection into separate hooks, but it is the same thing
    // happening with the text field empty.
    std::string selectCode(code ? code : "");
    return scripting::Offer(pPlayer,
        scripting::GossipItemSelect{ scripting::RefOf(pPlayer),
                                     scripting::RefOf(pItem),
                                     sender, action, selectCode });
}

/**
 * @brief Dispatches creature quest accept hooks to scripting engines.
 *
 * @param pPlayer The player accepting the quest.
 * @param pCreature The quest giver creature.
 * @param pQuest The accepted quest.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnQuestAccept(Player* pPlayer, Creature* pCreature, Quest const* pQuest)
{
    return scripting::Offer(pPlayer,
            scripting::CreatureQuestAccept{ scripting::RefOf(pPlayer),
                                   scripting::RefOf(pCreature),
                                   scripting::HandleOf(pQuest) });
}

/**
 * @brief Dispatches game object quest accept hooks to scripting engines.
 *
 * @param pPlayer The player accepting the quest.
 * @param pGameObject The quest giver game object.
 * @param pQuest The accepted quest.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnQuestAccept(Player* pPlayer, GameObject* pGameObject, Quest const* pQuest)
{
    return scripting::Offer(pPlayer,
            scripting::GameobjectQuestAccept{ scripting::RefOf(pPlayer),
                                   scripting::RefOf(pGameObject),
                                   scripting::HandleOf(pQuest) });
}

/**
 * @brief Dispatches item quest accept hooks to scripting engines.
 *
 * @param pPlayer The player accepting the quest.
 * @param pItem The quest-starting item.
 * @param pQuest The accepted quest.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnQuestAccept(Player* pPlayer, Item* pItem, Quest const* pQuest)
{
    return scripting::Offer(pPlayer,
            scripting::ItemQuestAccept{ scripting::RefOf(pPlayer),
                                   scripting::RefOf(pItem),
                                   scripting::HandleOf(pQuest) });
}

/**
 * @brief Dispatches creature quest reward hooks to scripting engines.
 *
 * @param pPlayer The player receiving the reward.
 * @param pCreature The quest giver creature.
 * @param pQuest The rewarded quest.
 * @param reward The selected reward index or identifier.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnQuestRewarded(Player* pPlayer, Creature* pCreature, Quest const* pQuest, uint32 reward)
{
    return scripting::Offer(pPlayer,
            scripting::CreatureQuestReward{ scripting::RefOf(pPlayer),
                                   scripting::RefOf(pCreature),
                                   scripting::HandleOf(pQuest),
                                   reward });
}

/**
 * @brief Dispatches game object quest reward hooks to scripting engines.
 *
 * @param pPlayer The player receiving the reward.
 * @param pGameObject The quest giver game object.
 * @param pQuest The rewarded quest.
 * @param reward The selected reward index or identifier.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnQuestRewarded(Player* pPlayer, GameObject* pGameObject, Quest const* pQuest, uint32 reward)
{
    return scripting::Offer(pPlayer,
            scripting::GameobjectQuestReward{ scripting::RefOf(pPlayer),
                                   scripting::RefOf(pGameObject),
                                   scripting::HandleOf(pQuest),
                                   reward });
}

/**
 * @brief Queries scripted dialog status for a creature gossip source.
 *
 * @param pPlayer The player querying the dialog state.
 * @param pCreature The creature being queried.
 * @return uint32 The dialog status value.
 */
uint32 ScriptMgr::GetDialogStatus(Player* pPlayer, Creature* pCreature)
{
    // The verdict says whether an engine had an answer; the answer itself
    // comes back in the slot, because a dialog status is a value and Verdict
    // is not a place to put one.
    scripting::CreatureDialogStatus event{ scripting::RefOf(pPlayer),
                                           scripting::RefOf(pCreature),
                                           DIALOG_STATUS_UNDEFINED };

    return scripting::Offer(pPlayer, event) ? event.status
                                            : DIALOG_STATUS_UNDEFINED;
}

/**
 * @brief Queries scripted dialog status for a game object gossip source.
 *
 * @param pPlayer The player querying the dialog state.
 * @param pGameObject The game object being queried.
 * @return uint32 The dialog status value.
 */
uint32 ScriptMgr::GetDialogStatus(Player* pPlayer, GameObject* pGameObject)
{
    scripting::GameobjectDialogStatus event{ scripting::RefOf(pPlayer),
                                             scripting::RefOf(pGameObject),
                                             DIALOG_STATUS_UNDEFINED };

    return scripting::Offer(pPlayer, event) ? event.status
                                            : DIALOG_STATUS_UNDEFINED;
}

/**
 * @brief Dispatches item use hooks to scripting engines.
 *
 * @param pPlayer The player using the item.
 * @param pItem The used item.
 * @param targets The item spell cast targets.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnItemUse(Player* pPlayer, Item* pItem, SpellCastTargets const& targets)
{
    // A refusal here means the scripts blocked the cast, and the caller
    // reads that as "handled" -- the opposite polarity to the claims
    // above, which is exactly why this one is an Ask and not an Offer.
    return scripting::Ask(pPlayer,
               scripting::ItemUse{ scripting::RefOf(pPlayer),
                                   scripting::RefOf(pItem),
                                   scripting::Lend(
                                       scripting::Domain::CastTargets,
                                       &targets) })
               == scripting::Verdict::Cancel;
}

/**
 * @brief Dispatches area trigger hooks to scripting engines.
 *
 * @param pPlayer The player entering the trigger.
 * @param atEntry The area trigger entry.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnAreaTrigger(Player* pPlayer, AreaTriggerEntry const* atEntry)
{
    return scripting::Offer(pPlayer,
        scripting::ServerEventTrigger{
            scripting::RefOf(pPlayer),
            scripting::HandleOf(scripting::Domain::AreaTrigger,
                                atEntry->id) });
}

/**
 * @brief Dispatches npc spell click hooks to scripting engines.
 *
 * @param pPlayer The player clicking the NPC spell interaction.
 * @param pClickedCreature The clicked creature.
 * @param spellId The triggering spell id.
 * @return true if a script handled the event; otherwise false.
 */
bool ScriptMgr::OnNpcSpellClick(Player* pPlayer, Creature* pClickedCreature, uint32 spellId)
{
    // Nothing in this core calls this. The hook stays, converted, because the
    // back end behind it implements the handler and deleting the last caller
    // of something is how a feature disappears without anyone deciding to
    // remove it -- 2.4.3 simply has no spell-click table to drive it from.
    return scripting::Offer(pPlayer,
        scripting::CoreNpcSpellClick{ scripting::RefOf(pPlayer),
                                      scripting::RefOf(pClickedCreature),
                                      spellId });
}

/**
 * @brief Dispatches dummy spell effect hooks for unit targets.
 *
 * @param pCaster The spell caster.
 * @param spellId The triggering spell id.
 * @param effIndex The spell effect index.
 * @param pTarget The unit target.
 * @param originalCasterGuid The original caster guid.
 * @return true if a script handled the effect; otherwise false.
 */
bool ScriptMgr::OnEffectDummy(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Unit* pTarget, ObjectGuid originalCasterGuid)
{
    // Two events, and they are not the same statement. The typed one says a
    // dummy effect landed on a CREATURE and is not raised for anything else;
    // the core one says a dummy effect landed, keyed by spell, and is raised
    // for every target a dummy effect can have -- including a player, which
    // no typed event describes.
    if (Creature* creature = pTarget->ToCreature())
    {
        scripting::Notify(pCaster,
            scripting::CreatureDummyEffect{ scripting::RefOf(pCaster),
                                            spellId,
                                            static_cast<uint32>(effIndex),
                                            scripting::RefOf(creature) });
    }

    return scripting::Offer(pCaster,
        scripting::CoreEffectDummy{ scripting::RefOf(pCaster), spellId,
                                    static_cast<uint32>(effIndex),
                                    scripting::RefOf(pTarget),
                                    scripting::Ref{ originalCasterGuid.GetRawValue() } });
}

/**
 * @brief Dispatches dummy spell effect hooks for game object targets.
 *
 * @param pCaster The spell caster.
 * @param spellId The triggering spell id.
 * @param effIndex The spell effect index.
 * @param pTarget The game object target.
 * @param originalCasterGuid The original caster guid.
 * @return true if a script handled the effect; otherwise false.
 */
bool ScriptMgr::OnEffectDummy(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, GameObject* pTarget, ObjectGuid originalCasterGuid)
{
    scripting::Notify(pCaster,
        scripting::GameobjectDummyEffect{ scripting::RefOf(pCaster),
                                          spellId,
                                          static_cast<uint32>(effIndex),
                                          scripting::RefOf(pTarget) });

    return scripting::Offer(pCaster,
        scripting::CoreEffectDummy{ scripting::RefOf(pCaster), spellId,
                                    static_cast<uint32>(effIndex),
                                    scripting::RefOf(pTarget),
                                    scripting::Ref{ originalCasterGuid.GetRawValue() } });
}

/**
 * @brief Dispatches dummy spell effect hooks for item targets.
 *
 * @param pCaster The spell caster.
 * @param spellId The triggering spell id.
 * @param effIndex The spell effect index.
 * @param pTarget The item target.
 * @param originalCasterGuid The original caster guid.
 * @return true if a script handled the effect; otherwise false.
 */
bool ScriptMgr::OnEffectDummy(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Item* pTarget, ObjectGuid originalCasterGuid)
{
    scripting::Notify(pCaster,
        scripting::ItemDummyEffect{ scripting::RefOf(pCaster),
                                    spellId,
                                    static_cast<uint32>(effIndex),
                                    scripting::RefOf(pTarget) });

    return scripting::Offer(pCaster,
        scripting::CoreEffectDummy{ scripting::RefOf(pCaster), spellId,
                                    static_cast<uint32>(effIndex),
                                    scripting::RefOf(pTarget),
                                    scripting::Ref{ originalCasterGuid.GetRawValue() } });
}

/**
 * @brief Dispatches script-effect spell hooks for unit targets.
 *
 * @param pCaster The spell caster.
 * @param spellId The triggering spell id.
 * @param effIndex The spell effect index.
 * @param pTarget The unit target.
 * @param originalCasterGuid The original caster guid.
 * @return true if a script handled the effect; otherwise false.
 */
bool ScriptMgr::OnEffectScriptEffect(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Unit* pTarget, ObjectGuid originalCasterGuid)
{
    return scripting::Offer(pCaster,
        scripting::CoreEffectScriptEffect{ scripting::RefOf(pCaster), spellId,
                                           static_cast<uint32>(effIndex),
                                           scripting::RefOf(pTarget),
                                           scripting::Ref{ originalCasterGuid.GetRawValue() } });
}

/**
 * @brief Dispatches dummy aura application and removal hooks.
 *
 * @param pAura The aura being processed.
 * @param apply True when applying the aura; false when removing it.
 * @return true if a script handled the aura event; otherwise false.
 */
bool ScriptMgr::OnAuraDummy(Aura const* pAura, bool apply)
{
    // An Aura has no identity to hand out and no lifetime an engine can reason
    // about, so it travels as a borrow: an engine that stores it and reads it
    // next tick finds a stale epoch instead of freed memory.
    return scripting::Offer(pAura->GetTarget(),
        scripting::CoreAuraDummy{ scripting::Lend(scripting::Domain::Aura,
                                                  pAura),
                                  apply });
}
