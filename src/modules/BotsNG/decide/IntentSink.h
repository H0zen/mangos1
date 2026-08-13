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

#ifndef MANGOS_BOTSNG_INTENTSINK_H
#define MANGOS_BOTSNG_INTENTSINK_H

#include "Intent.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace bots
{
    /// How many proposals one tick may hold. Sixteen is not a guess: a layer
    /// proposes what it wants, not what it might want, and a bot with more
    /// than sixteen wants in one tick has a policy that needs splitting.
    constexpr std::size_t MaxProposals = 16;

    /**
     * Where the layers put what they want, and the only thing they may write.
     *
     * A layer's whole signature is `(Perception const&, IntentSink&)`, so what
     * it may touch is visible in one line: it reads a snapshot and it proposes.
     * There is no `Player*` to reach for, no context to look a value up in, no
     * queue to push a continuation onto -- and no allocation, because the store
     * is a fixed array.
     *
     * OVERFLOW IS NOT SILENT, and it is not first-come-first-served either. A
     * full sink keeps the highest-scoring proposals: a seventeenth proposal
     * that beats the worst one already held replaces it. Dropping the newest
     * arrival would mean the layer that ran last -- which is the one whose
     * whole purpose is to override the others -- is the one that gets ignored.
     */
    class IntentSink
    {
    public:
        /**
         * Offer @a intent. `ScoreNever` is dropped here rather than by the
         * arbiter, so a layer may say "not this, not ever" in the ordinary way
         * and cost nothing for it.
         */
        void Propose(Intent const& intent)
        {
            if (intent.score == ScoreNever)
            {
                return;
            }

            if (m_count < MaxProposals)
            {
                m_intents[m_count++] = intent;
                return;
            }

            std::size_t worst = 0;
            for (std::size_t i = 1; i < m_count; ++i)
            {
                if (m_intents[i].score < m_intents[worst].score)
                {
                    worst = i;
                }
            }

            ++m_dropped;
            if (intent.score > m_intents[worst].score)
            {
                m_intents[worst] = intent;
            }
        }

        std::size_t Count() const
        {
            return m_count;
        }

        Intent const& At(std::size_t index) const
        {
            return m_intents[index];
        }

        Intent const* begin() const
        {
            return m_intents.data();
        }

        Intent const* end() const
        {
            return m_intents.data() + m_count;
        }

        /// How many proposals did not fit. Nonzero means the policy is
        /// proposing more than it can possibly act on, which is worth a log
        /// once rather than a silent truncation every tick.
        std::size_t Dropped() const
        {
            return m_dropped;
        }

        void Clear()
        {
            m_count   = 0;
            m_dropped = 0;
        }

    private:
        std::array<Intent, MaxProposals> m_intents{};
        std::size_t                      m_count   = 0;
        std::size_t                      m_dropped = 0;
    };
}

#endif //MANGOS_BOTSNG_INTENTSINK_H
