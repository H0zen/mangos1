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

#include "ClientClock.h"

#include <algorithm>

namespace
{
    /**
     * @brief How much skew history to keep.
     *
     * SkewSince only ever looks back as far as the oldest running course. The longest
     * leg in the retail captures is 143.9 seconds, so a couple of minutes of history is
     * generous; the bound is on entries rather than time because the reports arrive in
     * bursts (the captures show runs of 66-70 ms reports, one per rendered frame on a
     * struggling client) and a burst must not evict the history a long flight needs.
     */
    constexpr size_t kSkewHistory = 256;

    /// Round trips above this are a stall, not a measurement, and are not allowed to
    /// widen the spread permanently.
    constexpr Helm::Millis kAbsurdRoundTrip = 5000;
}

namespace Helm
{
    void ClientClock::Sync(uint32 clientTicks, Instant sentAt, Instant answeredAt)
    {
        const Millis roundTrip = Since(answeredAt, sentAt);
        if (roundTrip < 0 || roundTrip > kAbsurdRoundTrip)
        {
            return;
        }

        // The answer describes the client at some point inside the round trip, and
        // nothing on the wire says where. The midpoint is the estimator; half the round
        // trip is its error bar, and carrying that error bar explicitly is what stops
        // the rest of the system from treating the offset as exact.
        const Instant midpoint = Advance(sentAt, roundTrip / 2);
        m_offset = Since(midpoint, Instant(clientTicks));
        m_spread = roundTrip / 2;
        m_lastSyncAt = answeredAt;
        m_known = true;
    }

    void ClientClock::Skipped(Millis ms, Instant when)
    {
        // Only ever positive on the wire, and a negative one would mean the client
        // claiming to have run ahead of itself. Refuse it rather than let it pay off a
        // debt that was really incurred.
        if (ms <= 0)
        {
            return;
        }

        m_skewTotal += ms;
        m_skew.emplace_back(when, m_skewTotal);
        while (m_skew.size() > kSkewHistory)
        {
            m_skew.pop_front();
        }
    }

    void ClientClock::Reset()
    {
        m_skew.clear();
        m_offset = 0;
        m_spread = 0;
        m_skewTotal = 0;
        m_lastSyncAt = 0;
        m_known = false;
    }

    Millis ClientClock::SkewSince(Instant when) const
    {
        // The cumulative total as of `when`, subtracted from the total now. Entries are
        // ordered, so this is the last entry at or before `when`; anything older than
        // the retained history counts as already settled, which is the safe direction
        // (it understates the debt rather than inventing one).
        Millis before = 0;
        for (auto const& entry : m_skew)
        {
            if (Since(entry.first, when) > 0)
            {
                break;
            }
            before = entry.second;
        }

        const Millis skew = m_skewTotal - before;
        return (skew > 0) ? skew : 0;
    }

    Millis ClientClock::Uncertainty(Instant now) const
    {
        if (!m_known)
        {
            // Nothing has been measured. Say so with a value large enough that no
            // caller mistakes it for precision: a course is worth about a second of
            // travel at a run, which is the scale of error an unsynchronised client can
            // hold.
            return 1000;
        }

        // The offset's own error, plus the time the client may have lost without having
        // been able to tell us yet -- a report takes one round trip to arrive, and the
        // round trip is what m_spread measures.
        Millis unknown = m_spread * 2;

        // A session that has stopped answering is a session whose clock is drifting
        // unobserved. The cadence is ten seconds; once two of them have gone by with no
        // answer, the offset is stale and the caller should be told.
        const Millis quiet = Since(now, m_lastSyncAt);
        if (quiet > 20000)
        {
            unknown += quiet - 20000;
        }

        return m_spread + unknown;
    }
}
