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

#ifndef MANGOS_H_INVENTORY
#define MANGOS_H_INVENTORY

#include "Platform/Define.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Item;
class Player;

/**
 * @brief What a player is carrying, and the queue that gets it saved.
 *
 * Held by value on Player. Owns the slots, the pending-save queue and the two
 * duration lists; the rules about which item may go in which slot stay on
 * Player, because they are questions about a player rather than about a box.
 *
 * THE QUEUE IS WHY THIS IS A CLASS. An Item remembers its own index into the
 * queue, and removal writes a tombstone AT that index. So the item's idea of
 * where it is and the vector's idea have to agree exactly: drift by one and
 * removing an item blanks somebody else's pending save, which is a lost item
 * with no error anywhere.
 *
 * The index is therefore never computed by a caller. Enqueue hands it back and
 * Dequeue takes it, so the only way to get a position is from the container
 * that owns the vector.
 */
class Inventory
{
    public:
        /// Not in the queue. The same sentinel Item uses.
        static const std::int16_t NotQueued = -1;

        explicit Inventory(Player* owner) : m_owner(owner) {}

        std::vector<Item*>&       Queue()       { return m_queue; }
        std::vector<Item*> const& Queue() const { return m_queue; }

        /**
         * @brief Put an item in the pending-save queue.
         *
         * @return where it landed, to be kept by the item -- or NotQueued when
         *         the queue is blocked and nothing was recorded.
         */
        std::int16_t Enqueue(Item* item);

        /**
         * @brief Blank the queue entry an item was holding.
         *
         * @param pos the position Enqueue returned. Out-of-range positions are
         *        ignored rather than trusted: this is the write that would
         *        otherwise blank a different item's pending save.
         */
        void Dequeue(std::int16_t pos);

        void ClearQueue() { m_queue.clear(); }

        /**
         * @brief Stop recording changes.
         *
         * Loading a character sets every field on every item, and none of that
         * is a change worth saving back. Both Enqueue and Dequeue go quiet
         * while it is set, which is what keeps an item's remembered position
         * honest: blocked, it never gets one.
         */
        void BlockQueue(bool blocked) { m_queueBlocked = blocked; }
        bool QueueBlocked() const     { return m_queueBlocked; }

    private:
        Player* m_owner;                    ///< non-owning

        std::vector<Item*> m_queue;
        bool m_queueBlocked = false;
};

#endif
