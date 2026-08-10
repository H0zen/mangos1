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

// Loading the compiled script library, and the free-function facade over the
// binding registry.

#include "ScriptBindings.h"

#include "Log.h"
#include "ObjectMgr.h"
#include "Policies/Singleton.h"
#include "SQLStorages.h"
#include "World.h"

#ifdef ENABLE_SD3
#include "engine/system/ScriptDevMgr.h"
#endif

#include <cstring>
#include <set>

INSTANTIATE_SINGLETON_1(ScriptBindings);

/**
 * @brief Loads or reloads the named script library.
 *
 * @param libName The script library name.
 * @return ScriptLoadResult The library loading result.
 */
ScriptLoadResult ScriptBindings::LoadScriptLibrary(const char* libName)
{
#ifdef ENABLE_SD3
    if (std::strcmp(libName, "mangosscript") == 0)
    {
        SD3::FreeScriptLibrary();
        SD3::InitScriptLibrary();
        return SCRIPT_LOAD_OK;
    }
#endif

    return SCRIPT_LOAD_ERR_NOT_FOUND;
}

/**
 * @brief Unloads the currently active script library.
 */
void ScriptBindings::UnloadScriptLibrary()
{
#ifdef ENABLE_SD3
    SD3::FreeScriptLibrary();
#else
    return;
#endif
}

/**
 * @brief Collects event ids that can legally start database event scripts.
 *
 * @param eventIds The set that receives discovered event ids.
 */
void ScriptBindings::CollectPossibleEventIds(std::set<uint32>& eventIds)
{
    // Load all possible script entries from gameobjects
    for (SQLStorageBase::SQLSIterator<GameObjectInfo> itr = sGOStorage.getDataBegin<GameObjectInfo>(); itr < sGOStorage.getDataEnd<GameObjectInfo>(); ++itr)
    {
        switch (itr->type)
        {
            case GAMEOBJECT_TYPE_GOOBER:
                eventIds.insert(itr->goober.eventId);
                break;
            case GAMEOBJECT_TYPE_CHEST:
                eventIds.insert(itr->chest.eventId);
                break;
            case GAMEOBJECT_TYPE_CAMERA:
                eventIds.insert(itr->camera.eventID);
                break;
            case GAMEOBJECT_TYPE_CAPTURE_POINT:
                eventIds.insert(itr->capturePoint.neutralEventID1);
                eventIds.insert(itr->capturePoint.neutralEventID2);
                eventIds.insert(itr->capturePoint.contestedEventID1);
                eventIds.insert(itr->capturePoint.contestedEventID2);
                eventIds.insert(itr->capturePoint.progressEventID1);
                eventIds.insert(itr->capturePoint.progressEventID2);
                eventIds.insert(itr->capturePoint.winEventID1);
                eventIds.insert(itr->capturePoint.winEventID2);
                break;
#if defined(WOTLK) || defined (CATA) || defined (MISTS)
            case GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING:
                eventIds.insert(itr->destructibleBuilding.damagedEvent);
                eventIds.insert(itr->destructibleBuilding.destroyedEvent);
                eventIds.insert(itr->destructibleBuilding.intactEvent);
                eventIds.insert(itr->destructibleBuilding.rebuildingEvent);
                break;
#endif
            default:
                break;
        }
    }

    // Load all possible script entries from spells
    for (uint32 i = 1; i < sSpellStore.GetNumRows(); ++i)
    {
        SpellEntry const* spell = sSpellStore.LookupEntry(i);
        if (spell)
        {
            for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
            {
#if defined (CATA)
                SpellEffectEntry const* spellEffect = spell->GetSpellEffect(SpellEffectIndex(j));
                if (!spellEffect)
                {
                    continue;
                }

                if (spellEffect->Effect == SPELL_EFFECT_SEND_EVENT)
                {
                    if (spellEffect->EffectMiscValue)
                    {
                        eventIds.insert(spellEffect->EffectMiscValue);
                    }
                }
#else
                if (spell->Effect[j] == SPELL_EFFECT_SEND_EVENT)
                {
                    if (spell->EffectMiscValue[j])
                    {
                        eventIds.insert(spell->EffectMiscValue[j]);
                    }
                }
#endif
            }
        }
    }
#if defined(TBC) || defined (WOTLK) || defined (CATA)
    // Load all possible event entries from taxi path nodes
    for (size_t path_idx = 0; path_idx < sTaxiPathNodesByPath.size(); ++path_idx)
    {
        for (size_t node_idx = 0; node_idx < sTaxiPathNodesByPath[path_idx].size(); ++node_idx)
        {
            TaxiPathNodeEntry const& node = sTaxiPathNodesByPath[path_idx][node_idx];

            if (node.ArrivalEventID)
            {
                eventIds.insert(node.ArrivalEventID);
            }

            if (node.DepartureEventID)
            {
                eventIds.insert(node.DepartureEventID);
            }
        }
    }
#endif
}

// Starters for events

uint32 GetScriptId(const char* name)
{
    return sScriptBindings.GetScriptId(name);
}

/**
 * @brief Returns the script name for a script id.
 *
 * @param id The internal script id.
 * @return char const* The matching script name.
 */
char const* GetScriptName(uint32 id)
{
    return sScriptBindings.GetScriptName(id);
}

/**
 * @brief Returns the number of registered script ids.
 *
 * @return uint32 The count of registered script ids.
 */
uint32 GetScriptIdsCount()
{
    return sScriptBindings.GetScriptIdsCount();
}

/**
 * @brief Sets the external waypoint table used by the waypoint manager.
 *
 * @param tableName The external waypoint table name.
 */
void SetExternalWaypointTable(char const* tableName)
{
    sWaypointMgr.SetExternalWPTable(tableName);
}

/**
 * @brief Adds a waypoint node from an external waypoint table.
 *
 * @param entry The creature entry owning the path.
 * @param pathId The path identifier.
 * @param pointId The waypoint point identifier.
 * @param x The waypoint X coordinate.
 * @param y The waypoint Y coordinate.
 * @param z The waypoint Z coordinate.
 * @param o The waypoint orientation.
 * @param waittime The wait time at the node.
 * @return true if the waypoint was added; otherwise false.
 */
bool AddWaypointFromExternal(uint32 entry, int32 pathId, uint32 pointId, float x, float y, float z, float o, uint32 waittime)
{
    return sWaypointMgr.AddExternalNode(entry, pathId, pointId, x, y, z, o, waittime);
}
