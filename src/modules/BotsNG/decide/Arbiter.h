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

#ifndef MANGOS_BOTSNG_ARBITER_H
#define MANGOS_BOTSNG_ARBITER_H

#include "IntentSink.h"
#include "Percept.h"

#include <array>
#include <cstddef>

namespace bots
{
    /// One per channel, and no more: a tick's plan is at most an action, a
    /// movement and a target.
    constexpr std::size_t MaxPlan = 3;

    /**
     * What the bot will actually do this tick.
     *
     * Empty is a legitimate answer and the commonest one -- a bot mid-cast
     * with nothing urgent should do nothing, and the old engine's inability to
     * say so is why it kept re-deciding until it found something.
     */
    struct Plan
    {
        std::array<Intent, MaxPlan> intents{};
        std::size_t                 count = 0;

        /// Proposals that were weighed and lost. Kept as a count rather than a
        /// list because the interesting question in a log is "was there
        /// competition", and keeping the losers would mean copying variants
        /// nobody reads.
        std::size_t rejected = 0;

        bool Empty() const
        {
            return count == 0;
        }

        Intent const* begin() const
        {
            return intents.data();
        }

        Intent const* end() const
        {
            return intents.data() + count;
        }
    };

    /**
     * Choose, from everything the layers proposed, what fits this tick.
     *
     * A greedy pass over the proposals sorted by score, which is exact here
     * rather than approximate: the channels make the choice one-of-each, so
     * there is no combination a knapsack would find that taking the best of
     * each channel misses.
     *
     * TIES ARE BROKEN BY PROPOSAL ORDER, deliberately and by `std::stable_sort`
     * rather than by accident. That is what makes an ordered policy mean
     * something: two layers that want the same thing equally are settled by
     * which one the policy lists first. The old engine settled them by the
     * lexicographic order of the strategies' NAMES -- `map<string, Strategy*>`
     * iterated in a loop that returned the first match -- so renaming a
     * strategy changed behaviour.
     *
     * @param self      the snapshot's own view; the arbiter reads the global
     *                  cooldown, the power bar and whether a cast is in flight.
     * @param proposals what the layers offered, in the order they offered it.
     */
    Plan Arbitrate(Self const& self, IntentSink const& proposals);
}

#endif //MANGOS_BOTSNG_ARBITER_H
