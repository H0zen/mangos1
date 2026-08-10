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

#ifndef MANGOS_H_SCRIPTMGR
#define MANGOS_H_SCRIPTMGR

#include <unordered_map>
#include "Platform/Define.h"
#include "ObjectGuid.h"
#include "DBCEnums.h"
#include "dbscripts/DbScripts.h"

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <map>
#include <set>
#include <string>
#include <vector>
// SpellEntry, and nothing else. What is left of this class is the DB script
// store and the SD3 binding registry, and only CanSpellEffectStartDBScript
// names a world type at all -- the fifteen forward declarations that used to
// stand here belonged to the hook signatures, not to this class.
struct SpellEntry;

enum ScriptLoadResult
{
    SCRIPT_LOAD_OK,
    SCRIPT_LOAD_ERR_NOT_FOUND,
    SCRIPT_LOAD_ERR_WRONG_API,
    SCRIPT_LOAD_ERR_OUTDATED,
};

class ScriptMgr
{
    public:
        ScriptMgr();
        ~ScriptMgr();

        std::string GenerateNameToId(ScriptedObjectType sot, uint32 id);

        void LoadDbScripts(DBScriptType type);
        void LoadDbScriptStrings();

        void LoadScriptNames();
        void LoadScriptBinding();
        void LoadAreaTriggerScripts();
        void LoadEventIdScripts();
        void LoadSpellIdScripts();

        uint32 GetAreaTriggerScriptId(uint32 triggerId) const;
        uint32 GetEventIdScriptId(uint32 eventId) const;

        bool ReloadScriptBinding();

        ScriptChainMap const* GetScriptChainMap(DBScriptType type);

        const char* GetScriptName(uint32 id) const
        {
            return id < m_scriptNames.size() ? m_scriptNames[id].c_str() : "";
        }

        uint32 GetScriptId(const char* name) const;

        uint32 GetScriptIdsCount() const
        {
            return m_scriptNames.size();
        }

        uint32 GetBoundScriptId(ScriptedObjectType entity, int32 entry);

        ScriptLoadResult LoadScriptLibrary(const char* libName);
        void UnloadScriptLibrary();
        bool IsScriptLibraryLoaded() const
        {
#ifdef ENABLE_SD3
            return true;
#else
            return false;
#endif
        }

        uint32 IncreaseScheduledScriptsCount()
        {
            return (uint32)++m_scheduledScripts;
        }
        uint32 DecreaseScheduledScriptCount()
        {
            return (uint32)--m_scheduledScripts;
        }
        uint32 DecreaseScheduledScriptCount(size_t count)
        {
            return (uint32)(m_scheduledScripts -= count);
        }
        bool IsScriptScheduled() const
        {
            return m_scheduledScripts > 0;
        }
        static bool CanSpellEffectStartDBScript(SpellEntry const* spellinfo, SpellEffectIndex effIdx);




        char const* GetScriptLibraryVersion() const;

        // The two dozen hook forwarders that used to sit here are gone. Every
        // one of them was a call straight into scripting::Offer/Ask, so the
        // world reached the engines through a manager that managed nothing --
        // and paid for it by including this header, the DB script command set
        // and the binding registry with it, in a hundred files. What was not a
        // one-liner now lives in Scripting/WorldHooks.h; the rest is raised at
        // the call site, which is what the seam was for.

    private:
        void CollectPossibleEventIds(std::set<uint32>& eventIds);
        void LoadScripts(DBScriptType type);
        void CheckScriptTexts(std::set<int32>& ids);

        typedef std::vector<std::string> ScriptNameMap;
        typedef std::unordered_map<int32, uint32> EntryToScriptIdMap;

        EntryToScriptIdMap m_scriptBind[SCRIPTED_MAX_TYPE];

        ScriptNameMap      m_scriptNames;
        DBScripts          m_dbScripts;
#ifdef _DEBUG
        // mutex allowing to reload the script binding table; TODO just do it AWAY from any map update, e.g. right after sessions update
        std::shared_mutex m_bindMutex;
#endif /* _DEBUG */
        // atomic op counter for active scripts amount
        std::atomic<long> m_scheduledScripts;
        char __cache_guard[1024];
        std::mutex m_lock;
};

#define sScriptMgr MaNGOS::Singleton<ScriptMgr>::Instance()

/**
 * Returns the numeric script identifier for the specified script name.
 */
uint32 GetScriptId(const char* name);

/**
 * Returns the script name associated with the specified script identifier.
 */
char const* GetScriptName(uint32 id);

/**
 * Returns the number of registered script identifiers.
 */
uint32 GetScriptIdsCount();

/**
 * Returns the script identifier bound to the specified area trigger.
 */
uint32 GetAreaTriggerScriptId(uint32 triggerId);

/**
 * Returns the script identifier bound to the specified event id.
 */
uint32 GetEventIdScriptId(uint32 eventId);

/**
 * Sets the external waypoint table used for loading script waypoints.
 */
void SetExternalWaypointTable(char const* tableName);

/**
 * Adds a waypoint definition from the external waypoint source.
 */
bool AddWaypointFromExternal(uint32 entry, int32 pathId, uint32 pointId, float x, float y, float z, float o, uint32 waittime);

#endif
