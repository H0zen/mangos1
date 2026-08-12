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
 * @file ProcIndex.h
 * @brief Aggregate proc mask with lazy rebuild.
 *
 * Every proc event used to walk all of a unit's aura holders and reject nearly
 * all of them one at a time -- twice per swing, once for the attacker and once
 * for the victim, and again for every spell hit and periodic tick.
 * IsSpellProcEventCanTriggeredBy rejects a holder outright when
 * (procFlags & EventProcFlag) == 0, so the OR of every held aura's flags is
 * enough to answer "could anything here possibly fire" without the walk.
 *
 * The aggregate is recomputed on first use after any aura change rather than
 * maintained incrementally: an OR cannot be undone on removal without
 * per-bit refcounts, and aura changes are far rarer than the events that
 * consult the result.
 *
 * The class knows nothing about auras or units -- it is handed an enumerator
 * and stores what comes back. That is what lets it be tested against a stand-in
 * for Unit rather than only in a running server.
 */

#ifndef MANGOS_H_PROCINDEX
#define MANGOS_H_PROCINDEX

#include "Platform/Define.h"

/**
 * @brief Caches the union of the proc flags currently held by an owner.
 *
 * Starts clean and empty, which is correct for an owner that holds nothing.
 * The owner must call Invalidate() at every point where the set it would
 * enumerate changes -- and at every such point, or the cache can answer "no
 * proc possible" while a proc really was.
 */
class ProcIndex
{
    public:
        /// @brief Marks the cached aggregate stale.
        void Invalidate() { m_dirty = true; }

        /// @brief Drops the cache back to its empty, clean state.
        void Clear()
        {
            m_aggregate = 0;
            m_dirty = false;
        }

        /**
         * @brief Tells whether any held entry could match an event.
         *
         * @param eventFlags The PROC_FLAG_* mask of the event being resolved.
         * @param enumerate  Callable invoked as enumerate(sink), which must call
         *                   sink(uint32) once per held entry with that entry's
         *                   effective proc flags. Only called when the cache is
         *                   stale.
         * @return false when no held entry can match, and the caller may skip
         *         the scan entirely.
         */
        template <typename Enumerator>
        bool Matches(uint32 eventFlags, Enumerator const& enumerate) const
        {
            if (m_dirty)
            {
                uint32 aggregate = 0;
                enumerate([&aggregate](uint32 flags) { aggregate |= flags; });
                m_aggregate = aggregate;
                m_dirty = false;
            }

            return (m_aggregate & eventFlags) != 0;
        }

        /// @brief The cached aggregate. Meaningless while IsDirty() holds.
        uint32 GetAggregate() const { return m_aggregate; }

        /// @brief True when the next Matches() will rebuild.
        bool IsDirty() const { return m_dirty; }

    private:
        mutable uint32 m_aggregate = 0;
        mutable bool m_dirty = false;
};

#endif
