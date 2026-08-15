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

#ifndef MANGOS_COMBAT_MELEESWING_H
#define MANGOS_COMBAT_MELEESWING_H

#include "ReactionQueue.h"
#include "combat/pure/CombatTypes.h"

class Unit;

namespace Combat
{
    /**
     * @brief One white swing, end to end.
     *
     * Reads both profiles, builds the matchup and the table, resolves the
     * strike, asks the aura system for absorb and resist, and commits. Every
     * consequence goes into @p queue; nothing runs from inside here.
     *
     * @param depth How deep in a reaction chain this swing already is. A swing
     *              the player's attack timer started is zero; one an extra
     *              attack granted is one. Stamped onto everything the commit
     *              enqueues, so the chain terminates by arithmetic.
     */
    void PerformSwing(Unit& attacker, Unit& victim, Hand hand,
                      ReactionQueue& queue, std::uint8_t depth = 0);

    /**
     * @brief Runs queued reactions against the world.
     *
     * Both ends of every reaction are re-resolved from their guid here. A
     * reaction whose source or target has gone is dropped rather than
     * followed, and that is the whole of the lifetime handling -- there is no
     * pointer in the queue to dangle.
     */
    class WorldReactionSink : public ReactionSink
    {
        public:
            explicit WorldReactionSink(Unit& anchor) : m_anchor(anchor) {}

            void Run(Reaction const& reaction, ReactionQueue& queue) override;

        private:
            Unit& m_anchor;
    };
}

#endif
