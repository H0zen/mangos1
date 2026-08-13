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

#ifndef MANGOS_BOTSNG_SQUAD_H
#define MANGOS_BOTSNG_SQUAD_H

#include "decide/Percept.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

class Player;

namespace bots
{
    /**
     * What a group knows, worked out once and read by everyone in it.
     *
     * THE MIDDLE THAT WAS MISSING. A bot has its own state and the world has
     * its state, and between them sits everything that is true of a party
     * rather than of a person: who is tanking, who is hurt, who is out of
     * mana. Without somewhere to put it, every member re-derives it, so a
     * party of five walks the same member list five times a tick and a raid of
     * forty does it forty times -- and worse, they can disagree, because each
     * derived it at a slightly different moment.
     *
     * The roster is built once per tick by whichever member asks first, and
     * every other member of that group reads the same table. Ten bots in a
     * group cost one pass, not ten, and they cannot disagree about who is at
     * 12% health because they are all looking at the same row.
     *
     * NO GRID SEARCH HAPPENS HERE. A group knows its own members by guid; the
     * expensive question -- what else is standing nearby -- is a different
     * question and belongs to whatever layer first needs an answer to it.
     */
    class Squad
    {
    public:
        /// Rebuild from @a anyMember's group unless this tick already did.
        void Refresh(Player* anyMember, std::uint64_t tick);

        /**
         * Copy the roster into @a out, leaving out @a self.
         *
         * A bot is not its own ally: every layer that looks for "the most hurt
         * member" would otherwise find itself first while at full health and
         * silently mean something different from what it says.
         */
        void CopyInto(Perception& out, EntityId self) const;

        std::uint64_t LastTick() const
        {
            return m_tick;
        }

    private:
        std::array<Ally, MaxAllies> m_allies{};
        std::size_t                 m_count = 0;
        bool                        m_truncated = false;
        std::uint64_t               m_tick = 0;
    };

    /**
     * Every squad in the process, and how they go away.
     *
     * Keyed by group id, and swept rather than unregistered: a squad nobody
     * has refreshed for a while is dropped on the next sweep. That is
     * deliberately not a hook on group disband -- a hook is a second thing to
     * keep correct, and getting it wrong leaks a roster per group for the life
     * of the process, which is exactly the sort of bug that only shows up on a
     * server that has been up for a week.
     */
    class SquadBoard
    {
    public:
        /// The squad for @a bot's group, refreshed for @a tick. Null when the
        /// bot is alone -- a party of one is not a party.
        Squad* For(Player* bot, std::uint64_t tick);

        /// Drop squads untouched for @a staleTicks. Cheap and occasional; the
        /// runner calls it on a slow beat, not every tick.
        void Sweep(std::uint64_t tick, std::uint64_t staleTicks = 600);

        std::size_t Size() const
        {
            return m_squads.size();
        }

    private:
        std::unordered_map<std::uint32_t, Squad> m_squads;
    };
}

#endif //MANGOS_BOTSNG_SQUAD_H
