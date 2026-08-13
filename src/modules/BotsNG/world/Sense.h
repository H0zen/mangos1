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

#ifndef MANGOS_BOTSNG_SENSE_H
#define MANGOS_BOTSNG_SENSE_H

#include "decide/Percept.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Player;
struct SpellEntry;

namespace bots
{
    class Squad;

    /**
     * One bot's window onto the world, refreshed in stages.
     *
     * NOT EVERYTHING DECAYS AT THE SAME RATE, and pretending otherwise is what
     * makes a bot expensive. Health changes between two ticks; the contents of
     * a bag change a few times an hour; which spells a character knows changes
     * a few times a level. Reading all three every tick is waste, and reading
     * them on a wall-clock timer -- "recompute if more than a second old" --
     * buys the waste back as non-determinism, because what a bot decides then
     * depends on when it happened to look rather than on what is true.
     *
     * So the refresh is staged by CAUSE rather than by age:
     *
     *   every tick      health, power, the cooldowns of the spells this bot's
     *                   policy may actually propose, what it is casting, where
     *                   it is, what is hitting it
     *   on invalidation the bags -- rebuilt when something says they changed,
     *                   never on a timer
     *   from the squad  the party roster, built once for the whole group
     *
     * The result is deterministic: the same world in the same tick produces
     * the same snapshot, whoever asks and whenever they last asked.
     *
     * THE GLOBAL COOLDOWN IS THE BOT'S OWN ACCOUNTING, and it can be, because
     * every cast a bot makes goes through this module's executor. There is one
     * way in, so there is one place that knows when the last one started --
     * which is more precise than anything the core exposes, since
     * `GlobalCooldownMgr` answers whether a given spell is blocked and not for
     * how much longer.
     */
    class Senses
    {
    public:
        explicit Senses(Player* bot) : m_bot(bot)
        {
        }

        /**
         * Fill @a out with this tick's truth.
         *
         * @param squad the group roster, already refreshed for this tick, or
         *              null for a bot with no group.
         */
        void Refresh(Perception& out, Tuning const& tune, std::uint64_t tick,
                     std::uint32_t nowMs, Squad const* squad);

        /// Something changed in the bags. Called by whoever knows; costs a
        /// boolean now and one rebuild on the next refresh.
        void BagsChanged()
        {
            m_bagsDirty = true;
        }

        /// The spells this bot's policy may propose. Only these have their
        /// cooldowns sensed -- a bot with forty spells in its book proposes
        /// eight, and the other thirty-two are not worth a map lookup a tick.
        void Watch(std::vector<std::uint32_t> spells);

        /// The executor's report that something was refused, and why.
        void Refused(Rebuff const& rebuff)
        {
            m_lastRefusal = rebuff;
        }

        /// The executor's report that a cast started, which is what makes the
        /// global cooldown knowable without asking the core.
        void CastStarted(SpellEntry const* spellInfo, std::uint32_t nowMs);

    private:
        void RefreshBags();

        Player*                         m_bot = nullptr;
        std::array<Carried, MaxCarried> m_bag{};
        std::size_t                     m_bagCount = 0;
        bool                            m_bagsDirty = true;
        std::vector<std::uint32_t>      m_watched;
        Rebuff                          m_lastRefusal;
        std::uint32_t                   m_gcdEndsMs = 0;
    };
}

#endif //MANGOS_BOTSNG_SENSE_H
