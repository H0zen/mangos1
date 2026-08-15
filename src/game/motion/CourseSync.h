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
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#ifndef MANGOS_COURSESYNC_H
#define MANGOS_COURSESYNC_H

#include "ClientClock.h"
#include "Course.h"

/**
 * @brief When to repair a running course instead of replacing it.
 *
 * A client that has lost movement time is drawing a unit behind where the plan says it
 * is, and there are two ways to fix that. Replacing the course costs a fresh
 * monster-move -- 48 to 76 bytes in the captures, and it restarts the client's
 * animation. Repairing it costs eleven bytes and the client simply slides to where it
 * should be. On a leg of the length retail actually sends -- a 95th percentile of 12.5
 * seconds and a longest of 144 -- there is a lot of room between those two prices.
 *
 * Nothing in the packet is about flying: it names a unit and a fraction. The captures
 * suggest retail does not think so either. In the dungeon capture the packet went out
 * 2,075 times to 31 units, and 29 of those 31 never received a smooth (flying) leg at
 * all -- though that is weaker evidence than it sounds, because none of the 29 received
 * a monster-move either, so their courses arrived some other way and their flags cannot
 * be read. What can be said is that "flying" is not a criterion this capture supports;
 * "has a long course running" is.
 *
 * Reach::FlyingOnly is still the default, because it is the conservative reading of
 * evidence that does not settle the question. Reach::AnyCourse offers the same repair
 * to every long course, which is the same trade on the same terms -- and the captures
 * make the case for it: the longest leg seen is 425 seconds.
 */
namespace Helm
{
    class CourseSync
    {
        public:
            /// Which courses are eligible for repair.
            enum class Reach : uint8
            {
                FlyingOnly,  ///< What retail does. The safe default.
                AnyCourse    ///< The same mechanism, offered to everything long.
            };

            /**
             * @brief The cadence, measured.
             *
             * The captures carry no timestamps, so the interval was recovered by
             * anchoring on the client tick counters that ride in every time-sync
             * answer and interpolating between them. Across the four files the median
             * gap between consecutive syncs for one unit came out at 5273, 5404, 5502
             * and 5750 ms -- 2,471 intervals, the bulk of them from the dungeon
             * capture, all in the five-to-six second band.
             *
             * Those are CLIENT milliseconds, and the client's clock is slow: the
             * spacing of the ten-second time-sync anchors measures 9,131 to 9,631 ms
             * on its own clock. Scaled back, retail's interval is nearer 5.8 seconds
             * than 5.0.
             *
             * Five thousand is therefore deliberately a little eager, and that is
             * free: the packet is idempotent and carries the whole truth, so an extra
             * one costs eleven bytes and corrects nothing that was already right.
             */
            static constexpr Millis kCadence = 5000;

            /**
             * @brief Repair early once the client is estimated to be this far behind.
             *
             * The clock tells us how much movement time the client has lost; multiplied
             * by the course's speed that is a distance, and a unit a few yards from
             * where the server thinks it is has already broken a melee range check.
             * This is the part retail's fixed cadence cannot do, because retail's
             * cadence does not know about the client's own reports.
             */
            static constexpr float kSkewYards = 3.0f;

            /**
             * @brief Never repair twice inside this window.
             *
             * A burst of skip reports -- and the captures show bursts, dozens of 66 ms
             * reports in a row from a client rendering at fifteen frames a second --
             * must not turn into a burst of packets.
             */
            static constexpr Millis kFloor = 1000;

            /**
             * @brief Do not repair this close to the end.
             *
             * The client is about to arrive and the arrival is exact anyway, so a
             * correction here buys nothing and risks a visible twitch on the last few
             * yards. Retail agrees: across 484 syncs the largest progress value ever
             * sent was 0.9993, and not one reached 1.0.
             */
            static constexpr Millis kTailGuard = 1500;

            /// Per-mover bookkeeping. Small enough to sit beside the course itself.
            struct State
            {
                Instant nextAt = 0;
                uint32  courseId = 0;
                bool    armed = false;
            };

            /**
             * @brief Should a repair go out for this course right now?
             *
             * Rearms itself, so a caller that sends whenever this returns true will
             * settle onto the cadence. Returns false for everything ineligible: short
             * courses, finished courses, the tail, and -- under Reach::FlyingOnly --
             * anything not flying.
             *
             * @param state The mover's bookkeeping; reset automatically when the leg
             *              underneath it changes.
             * @param leg   The leg the client is walking -- from a Course, or from the
             *              spline the server is executing right now.
             * @param clock That client's clock, for the early trigger. An unfed clock
             *              simply reports no skew, which disables the early trigger and
             *              leaves the plain cadence -- the right answer for a creature
             *              nobody in particular controls.
             * @param now   Server instant.
             * @param reach Policy.
             */
            static bool Due(State& state, Leg const& leg, ClientClock const& clock,
                            Instant now, Reach reach);

            /// Is this leg the kind that may be repaired at all, under `reach`?
            static bool Eligible(Leg const& leg, Reach reach);
    };
}

#endif // MANGOS_COURSESYNC_H
