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

#ifndef MANGOS_SCRIPT_WORLD_HOOKS_H
#define MANGOS_SCRIPT_WORLD_HOOKS_H

#include "Platform/Define.h"
#include "DBCEnums.h"
#include "ObjectGuid.h"

struct AreaTriggerEntry;
class Aura;
class Creature;
class GameObject;
class Item;
class Player;
class SpellCastTargets;
class Unit;
class Quest;

/**
 * The handful of hooks whose payload is not one line at the call site.
 *
 * Most of the world raises an event inline -- two lines, no header but
 * ScriptHost.h -- and that is the shape a hook should have. These are the ones
 * where it would be a lie: a dialog status has to build an event, ask, and read
 * an answer back out of a slot; a dummy effect raises a typed event AND a
 * keyed-by-spell one; an item use inverts the polarity of the verdict, because
 * a refusal there is what the caller reads as "handled". Copying any of that
 * to twenty-five call sites is how the three of them drift apart.
 *
 * They were methods on ScriptMgr, which is what made ScriptMgr.h -- the DB
 * script command set, the binding registry, a singleton and a mutex -- an
 * include in roughly a hundred files across the world, most of which wanted
 * exactly one of these calls. They are free functions in `scripting` now
 * because that is what they always were: nothing about them belongs to a
 * manager of anything, and there is no state to manage. What they name is the
 * world's own vocabulary -- a player greeted a creature, a dummy effect landed
 * -- and the engines are reached the same way every other hook reaches them.
 *
 * Naming is the world's, not an engine's: GossipHello, not OnGossipHello. An
 * "On" prefix is what a listener calls its own handler; a call site is not
 * listening to anything, it is stating that the thing happened.
 */
namespace scripting
{
    /// @return true when an engine produced the menu, so the core skips its own.
    bool GossipHello(Player* player, Creature* creature);
    bool GossipHello(Player* player, GameObject* go);
    bool GossipHello(Player* player, Item* item);

    /// @a code is the text an entry box collected, or nullptr. One event, not
    /// two: a coded and an uncoded selection are the same thing happening with
    /// the field left empty.
    bool GossipSelect(Player* player, Creature* creature, uint32 sender,
                      uint32 action, char const* code);
    bool GossipSelect(Player* player, GameObject* go, uint32 sender,
                      uint32 action, char const* code);
    bool GossipSelect(Player* player, Item* item, uint32 sender,
                      uint32 action, char const* code);

    bool QuestAccept(Player* player, Creature* creature, Quest const* quest);
    bool QuestAccept(Player* player, GameObject* go, Quest const* quest);
    bool QuestAccept(Player* player, Item* item, Quest const* quest);

    bool QuestRewarded(Player* player, Creature* creature, Quest const* quest,
                       uint32 reward);
    bool QuestRewarded(Player* player, GameObject* go, Quest const* quest,
                       uint32 reward);

    /// DIALOG_STATUS_UNDEFINED when no engine had an answer, which is the
    /// caller's signal to work the status out for itself.
    uint32 DialogStatus(Player* player, Creature* creature);
    uint32 DialogStatus(Player* player, GameObject* go);

    /// @return true when the scripts REFUSED the use, which the caller treats
    ///         as "dealt with" -- the opposite polarity to everything above,
    ///         and the reason this one is asked rather than offered.
    bool ItemUsed(Player* player, Item* item, SpellCastTargets const& targets);

    bool AreaTriggered(Player* player, AreaTriggerEntry const* trigger);
    bool NpcSpellClicked(Player* player, Creature* clicked, uint32 spellId);

    /// Raises the typed event for what was hit AND the spell-keyed one, which
    /// are different statements: the typed events describe a creature, a game
    /// object or an item, and none of them describes a player.
    bool DummyEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                     Unit* target, ObjectGuid originalCaster);
    bool DummyEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                     GameObject* target, ObjectGuid originalCaster);
    bool DummyEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                     Item* target, ObjectGuid originalCaster);

    bool ScriptEffect(Unit* caster, uint32 spellId, SpellEffectIndex effIndex,
                      Unit* target, ObjectGuid originalCaster);

    bool DummyAura(Aura const* aura, bool apply);
}

#endif //MANGOS_SCRIPT_WORLD_HOOKS_H
