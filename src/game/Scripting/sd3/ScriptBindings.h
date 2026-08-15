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

#ifndef MANGOS_SD3_SCRIPT_BINDINGS_H
#define MANGOS_SD3_SCRIPT_BINDINGS_H

/**
 * Which compiled script answers for which entry.
 *
 * SD3's scripts are bound by NAME: a row in `script_binding` says that
 * creature entry 12345 is driven by the script registered as
 * "boss_onyxia", and this is what turns that name into the id the engine
 * looks up. Two tables and nothing else -- the names, in the order that
 * gives each one its id, and the entry-to-id map per kind of subject.
 *
 * It lived on ScriptMgr, next to the DB-script command set, which is how a
 * file that only wanted a creature's script id ended up including ninety
 * opcodes it had no use for. The two were never related: one is a compiled
 * script's identity, the other is an interpreted table's instruction set.
 *
 * The free functions at the bottom are the facade the world actually uses --
 * `GetScriptId("boss_onyxia")` reads better at a loader's call site than the
 * singleton does, and there are a few dozen such sites.
 */

#include "Platform/Define.h"
#include "Policies/Singleton.h"

#include <shared_mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

enum ScriptedObjectType
{
    SCRIPTED_UNIT           = 0,    //CreatureScript
    SCRIPTED_GAMEOBJECT     = 1,    //GameObjectScript
    SCRIPTED_ITEM           = 2,    //ItemScript
    SCRIPTED_AREATRIGGER    = 3,    //AreaTriggerScript
    SCRIPTED_SPELL          = 4,    //SpellScript
    SCRIPTED_AURASPELL      = 5,    //AuraScript
    SCRIPTED_MAPEVENT       = 6,    //MapEventScript
    SCRIPTED_MAP            = 7,    //ZoneScript
    SCRIPTED_BATTLEGROUND   = 8,    //BattleGroundScript
    SCRIPTED_PVP_ZONE       = 9,    //OutdoorPvPScript
    SCRIPTED_INSTANCE       = 10,   //InstanceScript
    SCRIPTED_CONDITION      = 11,   //ConditionScript
    SCRIPTED_ACHIEVEMENT    = 12,   //AchievementScript
    SCRIPTED_MAX_TYPE
};

enum ScriptImplementation
{
    SCRIPT_FROM_DATABASE    = 0,
    SCRIPT_FROM_CORE        = 1,
};

/// What LoadScriptLibrary made of the request.
enum ScriptLoadResult
{
    SCRIPT_LOAD_OK,
    SCRIPT_LOAD_ERR_NOT_FOUND,
    SCRIPT_LOAD_ERR_WRONG_API,
    SCRIPT_LOAD_ERR_OUTDATED,
};

class ScriptBindings
{
    public:
        std::string GenerateNameToId(ScriptedObjectType sot, uint32 id);

        void LoadScriptNames();
        void LoadScriptBinding();
        void LoadAreaTriggerScripts();
        void LoadEventIdScripts();
        void LoadSpellIdScripts();

        bool ReloadScriptBinding();

        uint32 GetAreaTriggerScriptId(uint32 triggerId) const;
        uint32 GetEventIdScriptId(uint32 eventId) const;

        char const* GetScriptName(uint32 id) const
        {
            return id < m_scriptNames.size() ? m_scriptNames[id].c_str() : "";
        }

        uint32 GetScriptId(char const* name) const;

        uint32 GetScriptIdsCount() const
        {
            return m_scriptNames.size();
        }

        /// 0 when nothing is bound, which is the ordinary case: most entries
        /// in the world have no compiled script at all.
        uint32 GetBoundScriptId(ScriptedObjectType entity, int32 entry);

        ScriptLoadResult LoadScriptLibrary(char const* libName);
        void UnloadScriptLibrary();
        char const* GetScriptLibraryVersion() const;

        bool IsScriptLibraryLoaded() const
        {
#ifdef ENABLE_SD3
            return true;
#else
            return false;
#endif
        }

    private:
        typedef std::vector<std::string> ScriptNameMap;
        typedef std::unordered_map<int32, uint32> EntryToScriptIdMap;

        EntryToScriptIdMap m_scriptBind[SCRIPTED_MAX_TYPE];
        ScriptNameMap      m_scriptNames;
#ifdef _DEBUG
        // Guards a reload of the binding table. TODO: do the reload away from
        // any map update instead -- right after the sessions update -- and the
        // mutex stops being needed at all.
        mutable std::shared_mutex m_bindMutex;
#endif /* _DEBUG */
};

#define sScriptBindings MaNGOS::Singleton<ScriptBindings>::Instance()

/// The facade. Same answers, spelt the way a loader wants to read them.
uint32 GetScriptId(char const* name);
char const* GetScriptName(uint32 id);
uint32 GetScriptIdsCount();
uint32 GetAreaTriggerScriptId(uint32 triggerId);
uint32 GetEventIdScriptId(uint32 eventId);

/// Waypoints an SD3 script adds by hand, rather than from creature_movement.
void SetExternalWaypointTable(char const* tableName);
bool AddWaypointFromExternal(uint32 entry, int32 pathId, uint32 pointId,
                             float x, float y, float z, float o,
                             uint32 waittime);

#endif //MANGOS_SD3_SCRIPT_BINDINGS_H
