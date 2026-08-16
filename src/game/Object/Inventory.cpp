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

#include "Inventory.h"

std::int16_t Inventory::Enqueue(Item* item)
{
    if (m_queueBlocked)
    {
        return NotQueued;
    }

    m_queue.push_back(item);
    return static_cast<std::int16_t>(m_queue.size() - 1);
}

void Inventory::Dequeue(std::int16_t pos)
{
    if (m_queueBlocked || pos == NotQueued)
    {
        return;
    }

    // Bounds-checked because this write is the one that can do damage. A
    // position that no longer addresses this queue -- a stale one kept across
    // a clear, say -- would otherwise blank whatever item happens to sit
    // there now, and a pending save that quietly becomes NULL is an item the
    // player loses with nothing logged.
    if (pos < 0 || static_cast<std::size_t>(pos) >= m_queue.size())
    {
        return;
    }

    m_queue[pos] = nullptr;
}
