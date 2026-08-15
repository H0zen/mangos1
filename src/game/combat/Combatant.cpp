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

#include "Combatant.h"

#include "ProfileBuilder.h"

#include "Log.h"
#include "Unit.h"

namespace Combat
{
    Profile const& Combatant::Read()
    {
        if (m_stale)
        {
            m_profile = BuildProfile(m_owner);
            m_profile.version = ++m_builds;
            m_stale = false;

            return m_profile;
        }

#ifdef MANGOS_DEBUG
        // The cache checking itself. Costs exactly what not caching cost, so
        // it is only here -- but it is here on every read, because a missed
        // invalidation shows up on the read and not on the change.
        Profile fresh = BuildProfile(m_owner);
        fresh.version = m_profile.version;

        if (char const* field = FirstDifference(m_profile, fresh))
        {
            sLog.outError("Combatant: %s served a stale profile -- %s drifted. "
                          "Something changed it without saying so.",
                          m_owner ? m_owner->GetGuidStr().c_str() : "<none>",
                          field);

            m_profile = fresh;
        }
#endif

        return m_profile;
    }
}
