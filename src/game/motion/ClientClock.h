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

#ifndef MANGOS_CLIENTCLOCK_H
#define MANGOS_CLIENTCLOCK_H

#include "CourseTime.h"
#include "Platform/Define.h"

#include <deque>
#include <mutex>
#include <utility>

/**
 * @brief One session's model of the clock the CLIENT interpolates courses against.
 *
 * A course begins, for the server, when it is laid, and for the client when the packet
 * lands -- no timestamp goes on the wire, only a duration. That gap would be unknowable
 * except that the protocol carries two facts that close it, and they are the whole
 * reason this class exists:
 *
 *  - the server asks `SMSG_TIME_SYNC_REQ` and the client answers with its own tick
 *    counter. The retail captures show this on a fixed cadence -- the gap between
 *    consecutive answers has a median of 9589, 9631 and 9626 ms in the three files, so
 *    ten seconds nominal, and the same in all of them.
 *
 *  - the client volunteers `CMSG_MOVE_TIME_SKIPPED` whenever its own movement clock
 *    loses time. It is not a rare event: 1,198 reports across the three captures, a
 *    median of 74 to 189 ms each, a 95th percentile near 1.4 seconds and single reports
 *    as large as 29.4 seconds. Never zero, never negative -- the client only ever
 *    admits to falling behind.
 *
 * So the client's movement clock is not merely offset from the server's, it LAGS, by an
 * amount the client itself reports. Track the lag and the server stops guessing: it can
 * say how far behind the client is, and therefore how far the position it is drawing
 * differs from the plan.
 */
namespace Helm
{
    class ClientClock
    {
        public:
            /**
             * @brief Fold in one completed time-sync round trip.
             *
             * @param clientTicks The counter the client returned.
             * @param sentAt      When the request went out.
             * @param answeredAt  When the answer arrived.
             *
             * The offset is taken at the midpoint of the round trip, which is the best
             * a one-way-uncertain link allows, and the round trip itself is retained
             * because it bounds how wrong that assumption can be.
             */
            void Sync(uint32 clientTicks, Instant sentAt, Instant answeredAt);

            /**
             * @brief The client reports having lost `ms` of movement time.
             *
             * Accumulated rather than averaged: this is not noise to be filtered, it is
             * a debt. Every millisecond reported is a millisecond of course the client
             * has not walked and the server has.
             */
            void Skipped(Millis ms, Instant when);

            /// The session dropped its mover or teleported; the debt is settled.
            void Reset();

            bool Known() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_known;
            }

            /// Server instant corresponding to a client tick stamp.
            Instant ToServer(uint32 clientTicks) const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return Instant(clientTicks + uint32(m_offset));
            }

            /// Half the best observed round trip: the irreducible error in the offset.
            Millis Spread() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_spread;
            }

            /**
             * @brief Milliseconds of movement time the client has lost since `when`.
             *
             * This is the number that lets a course be evaluated in the client's frame
             * rather than the server's, and it is measured, not modelled.
             */
            Millis SkewSince(Instant when) const;

            /**
             * @brief How wrong the server's idea of the client's movement clock may be,
             *        right now, in milliseconds.
             *
             * The offset's own spread, plus an allowance for time the client has lost
             * but not yet had a chance to report -- bounded by one round trip, because
             * that is how long a report takes to arrive.
             */
            Millis Uncertainty(Instant now) const;

            /// Total lag accumulated since the last Reset. Diagnostics.
            Millis TotalSkew() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_skewTotal;
            }

            /// When the last sync answer landed. Diagnostics, and the sync scheduler.
            Instant LastSyncAt() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_lastSyncAt;
            }

        private:
            /**
             * @brief Guards every field below. Held only across arithmetic.
             *
             * This clock is written from TWO threads and read from a third position in
             * the tick, which is not obvious from any one call site:
             *
             *  - CMSG_TIME_SYNC_RESP is PROCESS_INPLACE, so Sync() runs on the NETWORK
             *    thread, the moment the packet lands;
             *  - CMSG_MOVE_TIME_SKIPPED is PROCESS_THREADSAFE, so Skipped() runs inside
             *    Map::Update on that map's worker;
             *  - Uncertainty() and SkewSince() are read by MaintainCourseSync, later in
             *    the same Map::Update.
             *
             * m_skew is a deque. A push_front/pop_front racing a walk of the same deque
             * is not a stale read, it is a walk through freed nodes.
             *
             * The lock, rather than deferring the sync packet to the map tick: the round
             * trip is measured with getMSTime() inside the handler, so deferring it by
             * up to a tick would inflate every measurement by that tick and shift the
             * offset by half of it. Fixing a race by corrupting the measurement the
             * whole class exists to make is not a fix.
             */
            mutable std::mutex m_mutex;

            /// (instant, cumulative skew at that instant), oldest first.
            std::deque<std::pair<Instant, Millis>> m_skew;

            Millis  m_offset = 0;      ///< server = client + offset
            Millis  m_spread = 0;      ///< half the best round trip
            Millis  m_skewTotal = 0;
            /// Cumulative debt already dropped from the front of m_skew. A
            /// SkewSince older than the retained history uses this rather
            /// than 0, so eviction understates the remaining debt instead of
            /// returning the whole session total.
            Millis  m_skewFloor = 0;
            Instant m_lastSyncAt = 0;
            bool    m_known = false;
    };
}

#endif // MANGOS_CLIENTCLOCK_H
