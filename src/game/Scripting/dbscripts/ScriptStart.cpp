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

// The two things the world still asks this corner of the tree, and nothing
// else. Both are about STARTING something; neither reads a table.
//
// WHAT WAS HERE. DbScriptStore -- ten `dbscripts_on_*` chain maps, the loaders
// that filled them, the locale-text check over them and an atomic count of
// scheduled steps -- lived in DbScriptStore.{h,cpp} and DbScriptTables.cpp,
// about a thousand lines. MAI reads the sequences now, from `mai_script` and
// `mai_step` through its own model, and keeps its own frames; nothing had
// called LoadDbScripts since. An unloaded store is not inert, either: it
// answers every lookup with "no such script", and two loaders believed it --
// 199 gossip rows and 507 waypoint nodes were being dropped at start-up
// because a chain map that nobody fills said their `script_id` did not exist.
//
// ScriptAction and the ScriptInfo vocabulary in DbScripts.h are NOT part of
// that and stay: they are the bodies MAI borrows for the verbs it has not
// rewritten yet, and they go one at a time as it does.

#include "DbScripts.h"

// StartEvents_Event raises an event, so this file is a call site like
// any other.
#include "ScriptHost.h"

#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "World.h"

#include "BattleGround/BattleGround.h"
#include "OutdoorPvP/OutdoorPvP.h"

bool StartEvents_Event(Map* map, uint32 id, Object* source, Object* target, bool isStart/*=true*/, Unit* forwardToPvp/*=NULL*/)
{
    MANGOS_ASSERT(source);

    // Handle PvP Calls
    if (forwardToPvp && source->GetTypeId() == TYPEID_GAMEOBJECT)
    {
        BattleGround* bg = NULL;
        OutdoorPvP* opvp = NULL;
        if (forwardToPvp->GetTypeId() == TYPEID_PLAYER)
        {
            bg = ((Player*)forwardToPvp)->GetBattleGround();
            if (!bg)
            {
                opvp = sOutdoorPvPMgr.GetScript(((Player*)forwardToPvp)->GetCachedZoneId());
            }
        }
        else
        {
#if defined(CLASSIC)
            if (map->IsBattleGround())
#else
            if (map->IsBattleGroundOrArena())
#endif
            {
                bg = ((BattleGroundMap*)map)->GetBG();
            }
            else                                            // Use the go, because GOs don't move
            {
                GameObject const* go = static_cast<GameObject*>(source);
                opvp = sOutdoorPvPMgr.GetScript(go->GetTerrain()->GetZoneId(
                           go->Where().X(), go->Where().Y(), go->Where().Z()));
            }
        }

        if (bg && bg->HandleEvent(id, static_cast<GameObject*>(source)))
        {
            return true;
        }

        if (opvp && opvp->HandleEvent(id, static_cast<GameObject*>(source)))
        {
            return true;
        }
    }

    return scripting::Offer(map,
               scripting::ServerEventRaised{ scripting::RefOf(source),
                                             scripting::RefOf(target),
                                             id, isStart });
}

namespace
{
    /// How strong a claim @a effIdx has on starting the spell's script, or 0
    /// for none. Higher wins; see SpellEffectStartsScript for the tie-break.
    uint8 StartPriority(SpellEntry const* spellinfo, SpellEffectIndex effIdx)
    {
        if (spellinfo->Effect[effIdx] == SPELL_EFFECT_SCRIPT_EFFECT)
        {
            return 10;
        }

        if (spellinfo->Effect[effIdx] == SPELL_EFFECT_DUMMY)
        {
            return 9;
        }

        // NonExisting triggered spells can also start DB-Spell-Scripts
        if (spellinfo->Effect[effIdx] == SPELL_EFFECT_TRIGGER_SPELL &&
            !sSpellStore.LookupEntry(spellinfo->EffectTriggerSpell[effIdx]))
        {
            return 5;
        }

        // NonExisting trigger missile spells can also start DB-Spell-Scripts
        if (spellinfo->Effect[effIdx] == SPELL_EFFECT_TRIGGER_MISSILE &&
            !sSpellStore.LookupEntry(spellinfo->EffectTriggerSpell[effIdx]))
        {
            return 4;
        }

        // Can not start script
        return 0;
    }
}

// Priorize: SCRIPT_EFFECT before DUMMY before Non-Existing triggered spell,
// for same priority the first effect with the priority triggers
bool SpellEffectStartsScript(SpellEntry const* spellinfo, SpellEffectIndex effIdx)
{
    uint8 priority = StartPriority(spellinfo, effIdx);
    if (!priority)
    {
        return false;
    }

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        uint8 currentPriority = StartPriority(spellinfo, SpellEffectIndex(i));
        if (currentPriority < priority)                     // lower priority, continue checking
        {
            continue;
        }
        if (currentPriority > priority)                     // take other index with higher priority
        {
            return false;
        }
        if (i < effIdx)                                     // same priority at lower index
        {
            return false;
        }
    }

    return true;
}
