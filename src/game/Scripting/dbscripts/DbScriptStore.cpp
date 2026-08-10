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

// What the DB-script store is made of, and the one function that starts a
// chain from outside it.

#include "DbScriptStore.h"

// StartEvents_Event raises an event, so this file is a call site like
// any other.
#include "ScriptHost.h"

#include "Log.h"
#include "Map.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Policies/Singleton.h"
#include "World.h"

#include "BattleGround/BattleGround.h"
#include "OutdoorPvP/OutdoorPvP.h"

DbScriptStore::DbScriptStore() : m_scheduledScripts(0)
{
    m_dbScripts.resize(DBS_END);

    ScriptChainMap emptyMap;

    for (int t = DBS_START; t < DBS_END; ++t)
    {
        m_dbScripts[t] = emptyMap;
    }
}

DbScriptStore::~DbScriptStore()
{
    m_dbScripts.clear();
}










// /////////////////////////////////////////////////////////
//              DB SCRIPT ENGINE
// /////////////////////////////////////////////////////////

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

// Wrappers
