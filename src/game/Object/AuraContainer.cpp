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

#include "AuraContainer.h"
#include "SpellAuras.h"

bool AuraContainer::Erase(SpellAuraHolder* holder)
{
    // Off the cursor before out of the map. A walk in progress holds an
    // iterator to the node about to be freed, and moving it afterwards would
    // be reading the node to find out where "afterwards" is.
    if (m_cursor != m_holders.end() && m_cursor->second == holder)
    {
        ++m_cursor;
    }

    Bounds bounds = BoundsOf(holder->GetId());
    for (HolderMap::iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        if (itr->second == holder)
        {
            m_holders.erase(itr);
            return true;
        }
    }

    return false;
}

void AuraContainer::UpdateHolders(uint32 diff)
{
    // The advance happens BEFORE the update, so a holder that removes itself
    // -- or any other -- while ticking cannot leave this walk on a freed node.
    // Erase() moves the cursor for the same reason from the other side.
    for (m_cursor = m_holders.begin(); m_cursor != m_holders.end();)
    {
        SpellAuraHolder* holder = m_cursor->second;
        ++m_cursor;
        holder->UpdateHolder(diff);
    }

    // The walk is over; a removal now has no cursor to disturb.
    m_cursor = m_holders.end();
}

void AuraContainer::Cleanup()
{
    for (HolderList::const_iterator itr = m_deletedHolders.begin();
         itr != m_deletedHolders.end(); ++itr)
    {
        delete *itr;
    }
    m_deletedHolders.clear();

    for (AuraList::const_iterator itr = m_deletedAuras.begin();
         itr != m_deletedAuras.end(); ++itr)
    {
        delete *itr;
    }
    m_deletedAuras.clear();
}
