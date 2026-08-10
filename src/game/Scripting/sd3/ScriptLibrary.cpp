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
#include "WaypointManager.h"
#include "World.h"

#ifdef ENABLE_SD3
#include "engine/system/ScriptDevMgr.h"
#endif

#include <cstring>
#include <set>

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
