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

#include "WorldHooks.h"
#include "ScriptHost.h"

#include "Creature.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "Item.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellAuras.h"

#include <string>

namespace scripting
{
    bool GossipHello(Player* player, Creature* creature)
    {
        return Offer(player, GossipCreatureHello{ RefOf(player),
                                                  RefOf(creature) });
    }

    bool GossipHello(Player* player, GameObject* go)
    {
        return Offer(player, GossipGameobjectHello{ RefOf(player),
                                                    RefOf(go) });
    }

    bool GossipHello(Player* player, Item* item)
    {
        return Offer(player, GossipItemHello{ RefOf(player), RefOf(item) });
    }

    bool GossipSelect(Player* player, Creature* creature, uint32 sender,
                      uint32 action, char const* code)
    {
        std::string text(code ? code : "");
        return Offer(player, GossipCreatureSelect{ RefOf(player),
                                                   RefOf(creature),
                                                   sender, action, text });
    }

    bool GossipSelect(Player* player, GameObject* go, uint32 sender,
                      uint32 action, char const* code)
    {
        std::string text(code ? code : "");
        return Offer(player, GossipGameobjectSelect{ RefOf(player), RefOf(go),
                                                     sender, action, text });
    }

    bool GossipSelect(Player* player, Item* item, uint32 sender,
                      uint32 action, char const* code)
    {
        std::string text(code ? code : "");
        return Offer(player, GossipItemSelect{ RefOf(player), RefOf(item),
                                               sender, action, text });
    }

    bool QuestAccept(Player* player, Creature* creature, Quest const* quest)
    {
        return Offer(player, CreatureQuestAccept{ RefOf(player),
                                                  RefOf(creature),
                                                  HandleOf(quest) });
    }

    bool QuestAccept(Player* player, GameObject* go, Quest const* quest)
    {
        return Offer(player, GameobjectQuestAccept{ RefOf(player), RefOf(go),
                                                    HandleOf(quest) });
    }

    bool QuestAccept(Player* player, Item* item, Quest const* quest)
    {
        return Offer(player, ItemQuestAccept{ RefOf(player), RefOf(item),
                                              HandleOf(quest) });
    }

    bool QuestRewarded(Player* player, Creature* creature, Quest const* quest,
                       uint32 reward)
    {
        return Offer(player, CreatureQuestReward{ RefOf(player),
                                                  RefOf(creature),
                                                  HandleOf(quest), reward });
    }

    bool QuestRewarded(Player* player, GameObject* go, Quest const* quest,
                       uint32 reward)
    {
        return Offer(player, GameobjectQuestReward{ RefOf(player), RefOf(go),
                                                    HandleOf(quest), reward });
    }

    // The verdict says whether an engine had an answer; the answer itself
    // comes back in the slot, because a dialog status is a value and Verdict is
    // not a place to put one.
    uint32 DialogStatus(Player* player, Creature* creature)
    {
        CreatureDialogStatus event{ RefOf(player), RefOf(creature),
                                    DIALOG_STATUS_UNDEFINED };

        return Offer(player, event) ? event.status : DIALOG_STATUS_UNDEFINED;
    }

    uint32 DialogStatus(Player* player, GameObject* go)
    {
        GameobjectDialogStatus event{ RefOf(player), RefOf(go),
                                      DIALOG_STATUS_UNDEFINED };

        return Offer(player, event) ? event.status : DIALOG_STATUS_UNDEFINED;
    }

    bool ItemUsed(Player* player, Item* item, SpellCastTargets const& targets)
    {
        // Ask, not Offer: a refusal here means the scripts blocked the cast,
        // and the caller reads that as "dealt with" -- the opposite polarity
        // to everything above.
        return Ask(player, ItemUse{ RefOf(player), RefOf(item),
                                    Lend(Domain::CastTargets, &targets) })
                   == Verdict::Cancel;
    }

    bool AreaTriggered(Player* player, AreaTriggerEntry const* trigger)
    {
        return Offer(player, ServerEventTrigger{
                                 RefOf(player),
                                 HandleOf(Domain::AreaTrigger, trigger->id) });
    }

    bool NpcSpellClicked(Player* player, Creature* clicked, uint32 spellId)
    {
        // Nothing in this core calls this. It stays because the back end
        // behind it implements the handler, and deleting the last caller of
        // something is how a feature disappears without anyone deciding to
        // remove it -- 2.4.3 simply has no spell-click table to drive it from.
        return Offer(player, CoreNpcSpellClick{ RefOf(player), RefOf(clicked),
                                                spellId });
    }

    // Two events, and they are not the same statement. The typed one says a
    // dummy effect landed on a CREATURE and is not raised for anything else;
    // the core one says a dummy effect landed, keyed by spell, and is raised
    // for every target a dummy effect can have -- including a player, which no
    // typed event describes.
    bool DummyEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                     Unit* target, ObjectGuid originalCaster)
    {
        if (Creature* creature = target->ToCreature())
        {
            Notify(caster, CreatureDummyEffect{ RefOf(caster), spellId,
                                                static_cast<uint32>(effIndex),
                                                RefOf(creature) });
        }

        return Offer(caster,
            CoreEffectDummy{ RefOf(caster), spellId,
                             static_cast<uint32>(effIndex), RefOf(target),
                             Ref{ originalCaster.GetRawValue() } });
    }

    bool DummyEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                     GameObject* target, ObjectGuid originalCaster)
    {
        Notify(caster, GameobjectDummyEffect{ RefOf(caster), spellId,
                                              static_cast<uint32>(effIndex),
                                              RefOf(target) });

        return Offer(caster,
            CoreEffectDummy{ RefOf(caster), spellId,
                             static_cast<uint32>(effIndex), RefOf(target),
                             Ref{ originalCaster.GetRawValue() } });
    }

    bool DummyEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                     Item* target, ObjectGuid originalCaster)
    {
        Notify(caster, ItemDummyEffect{ RefOf(caster), spellId,
                                        static_cast<uint32>(effIndex),
                                        RefOf(target) });

        return Offer(caster,
            CoreEffectDummy{ RefOf(caster), spellId,
                             static_cast<uint32>(effIndex), RefOf(target),
                             Ref{ originalCaster.GetRawValue() } });
    }

    bool ScriptEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                      Unit* target, ObjectGuid originalCaster)
    {
        return Offer(caster,
            CoreEffectScriptEffect{ RefOf(caster), spellId,
                                    static_cast<uint32>(effIndex),
                                    RefOf(target),
                                    Ref{ originalCaster.GetRawValue() } });
    }

    bool DummyAura(Aura const* aura, bool apply)
    {
        // An Aura has no identity to hand out and no lifetime an engine can
        // reason about, so it travels as a borrow: an engine that stores it
        // and reads it next tick finds a stale epoch instead of freed memory.
        return Offer(aura->GetTarget(),
                     CoreAuraDummy{ Lend(Domain::Aura, aura), apply });
    }
}
