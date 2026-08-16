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

#ifndef MANGOS_COMBAT_COMBATANT_H
#define MANGOS_COMBAT_COMBATANT_H

#include "combat/pure/Profile.h"

#include <cstdint>

class Unit;

namespace Combat
{
    /**
     * @brief What a unit is, as far as a strike is concerned.
     *
     * Held by value on Unit with a non-owning back-pointer, and it owns one
     * thing: the unit's Profile and whether it is still good.
     *
     * Building a Profile walks the aura lists, reads the equipment and does
     * the skill arithmetic. None of that changes between two swings unless
     * something changed, so it is done when something changes rather than
     * when something is hit.
     *
     * THE CACHE IS ONLY AS GOOD AS Invalidate(). A profile that is stale
     * because a call site forgot to say so is a wrong number in every swing
     * until an unrelated change clears it -- so a debug build rebuilds on
     * every read and compares, and names the field that drifted. A missed
     * invalidation is a line in the log the first time it happens rather than
     * a balance complaint months later.
     */
    class Combatant
    {
        public:
            explicit Combatant(Unit* owner) : m_owner(owner) {}

            /// The unit's profile, built if it is not current.
            Profile const& Read();

            /// Something a profile is derived from has changed.
            void Invalidate()
            {
                m_stale = true;
            }

            /// Whether the next Read will rebuild. For the tests and the log.
            bool IsStale() const
            {
                return m_stale;
            }

            /// How many times this unit's profile has been built. Zero means
            /// it has never been in a fight.
            std::uint32_t Builds() const
            {
                return m_builds;
            }

        private:
            Unit*   m_owner;
            Profile m_profile;
            bool    m_stale = true;

            std::uint32_t m_builds = 0;
    };
}

#endif
