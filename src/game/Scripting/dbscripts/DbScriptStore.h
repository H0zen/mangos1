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

#ifndef MANGOS_DBSCRIPT_STORE_H
#define MANGOS_DBSCRIPT_STORE_H

/**
 * The ten `dbscripts_on_*` tables, read once and handed out.
 *
 * One chain map per table type, each keyed by the id the world looks a script
 * up with -- a quest id, a creature entry, a spell id, a gameobject guid. What
 * runs those chains is ScriptAction, and what decides that one should start is
 * DbScriptEngine; this only owns the rows.
 *
 * The scheduled-script counter is here and not on a map, which is not obvious.
 * A DB script is queued on the map it will run on, so the count could have
 * been per map -- but the question it answers is "is anything anywhere still
 * pending", asked by the world when it wants to know whether it may shut down
 * or reload, and summing it across maps would mean walking them all under a
 * lock to answer a yes-or-no.
 */

#include "DbScripts.h"

#include "Policies/Singleton.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <set>

struct SpellEntry;

class DbScriptStore
{
    public:
        DbScriptStore();
        ~DbScriptStore();

        void LoadDbScripts(DBScriptType type);

        /// Last of all: the locale strings are checked against every script
        /// that references one, so every script has to be in first.
        void LoadDbScriptStrings();

        /// The chains for @a type, or nullptr when that type has none.
        ScriptChainMap const* GetScriptChainMap(DBScriptType type);

        /// Whether a spell effect is one a `dbscripts_on_spell` row may hang
        /// off. Static because it is a fact about the spell, not about what
        /// happens to be loaded.
        static bool CanSpellEffectStartDBScript(SpellEntry const* spellinfo,
                                                SpellEffectIndex effIdx);

        uint32 IncreaseScheduledScriptsCount()
        {
            return uint32(++m_scheduledScripts);
        }
        uint32 DecreaseScheduledScriptCount()
        {
            return uint32(--m_scheduledScripts);
        }
        uint32 DecreaseScheduledScriptCount(std::size_t count)
        {
            return uint32(m_scheduledScripts -= long(count));
        }
        bool IsScriptScheduled() const
        {
            return m_scheduledScripts > 0;
        }

    private:
        void LoadScripts(DBScriptType type);
        void CheckScriptTexts(std::set<int32>& ids);

        DBScripts m_dbScripts;

        // Counted across every map at once, so it has to be atomic; see the
        // note above for why it is not per map.
        std::atomic<long> m_scheduledScripts;

        char __cache_guard[1024];
        std::mutex m_lock;
};

#define sDbScripts MaNGOS::Singleton<DbScriptStore>::Instance()

#endif //MANGOS_DBSCRIPT_STORE_H
