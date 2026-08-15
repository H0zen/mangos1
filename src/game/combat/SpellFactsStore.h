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

#ifndef MANGOS_COMBAT_SPELLFACTSSTORE_H
#define MANGOS_COMBAT_SPELLFACTSSTORE_H

#include "combat/pure/SpellFacts.h"

#include <cstdint>
#include <vector>

namespace Combat
{
    /**
     * @brief Every spell's facts, decoded once at start-up.
     *
     * The spell audit's first finding is that nobody materialises anything:
     * duration, cast time, range and radius all stay DBC INDICES in the row,
     * and every question about them costs a second lookup in another store.
     * On a boss with thirty auras a single new damage-over-time spell can run
     * ninety lookups just to work out what it does not stack with.
     *
     * None of it depends on the world. All of it is a function of the DBC and
     * a handful of SQL overlays, both of which are fixed by the time the world
     * starts. So it is computed once, here, and read afterwards.
     *
     * Indexed by spell id, so a lookup is an array read. A spell the DBC does
     * not have returns a fact with @ref SpellFacts::known false rather than a
     * null pointer, which removes the branch every caller would otherwise
     * have to remember.
     */
    class SpellFactsStore
    {
        public:
            static SpellFactsStore& Instance();

            /// Read the DBC. Called once, after the DBC stores are loaded and
            /// before anything asks a spell a question.
            void Load();

            SpellFacts const& Get(std::uint32_t spellId) const
            {
                return spellId < m_facts.size() ? m_facts[spellId] : m_absent;
            }

            std::uint32_t Known() const
            {
                return m_known;
            }

            /**
             * @brief Compare every materialised fact against the live query.
             *
             * The gate that stops a wrong fact from ever being read. For each
             * spell in the DBC it asks the old question -- GetSpellDuration,
             * GetSpellCastTime, the range store -- and checks the answer
             * against what was decoded at load. A mismatch is reported with
             * the spell id and the field.
             *
             * This is the whole verification strategy for stage C: the store
             * is additive and nothing reads it, so the only way it can be
             * wrong is silently, and the only way to find that is to ask both.
             *
             * @return the number of mismatching fields.
             */
            std::uint32_t Audit() const;

        private:
            SpellFactsStore() = default;

            std::vector<SpellFacts> m_facts;
            SpellFacts              m_absent;
            std::uint32_t           m_known = 0;
    };
}

#define sSpellFacts Combat::SpellFactsStore::Instance()

#endif
