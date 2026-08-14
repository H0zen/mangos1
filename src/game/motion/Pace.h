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

#ifndef MANGOS_HELM_PACE_H
#define MANGOS_HELM_PACE_H

/**
 * @file Pace.h
 * @brief HOW FAST -- and therefore, on this wire, WHEN each point is reached.
 *
 * The second of the four values. It takes a `Path` and a speed and produces the client's
 * own timing of it: one mark per point, in milliseconds from the start of the leg.
 *
 * ## This is not a division
 *
 * The obvious implementation -- `duration = length / speed * 1000` -- disagrees with the
 * client on every leg, and the disagreement was measured rather than guessed. The client
 * accumulates, seeded at one and truncated per segment:
 *
 *     mark = 1
 *     for each segment:  mark = floor(mark + length3D(segment) * 1000 / speed)
 *
 * On 6,961 single-hop retail legs the residual `duration - length / speed * 1000` is
 * distributed uniformly on (0, 1] milliseconds -- not centred on zero, not on a half:
 * uniform on that interval, which is the fingerprint of an accumulator seeded at one and
 * truncated, and of nothing else. Two things follow that look like defects until the
 * distribution is understood:
 *
 *   - every leg is one millisecond long;
 *   - a 93-point leg loses up to 92 ms against the naive product, because each segment
 *     discards its own fraction independently.
 *
 * The leg is sent with a single duration and the client reparameterises its curve to
 * last exactly that, so both ends agree by construction. Computing the "true" duration
 * instead means the server and the client disagree about where the mover is for the
 * whole of every leg.
 *
 * ## Why it is its own value
 *
 * Because it is arithmetic over numbers and nothing else, it can be checked against the
 * captures without a path, a unit, a map or a clock -- and it is the single most reused
 * result in `MOTION.md`. Fusing it into a spline object, as the inherited code does, is
 * what made it untestable and therefore unverified for fifteen years.
 */

#include "motion/Path.h"

#include <cstdint>
#include <vector>

namespace Helm
{
    /**
     * @brief The client's timing of a path: when it reaches each point.
     *
     * `marks[0]` is 1, not 0. That is not an off-by-one to be tidied away -- it is the
     * seed, it is on the wire, and removing it makes every leg a millisecond short.
     */
    class Pace
    {
        public:
            Pace() = default;

            /**
             * @brief Time a path at a speed in yards per second.
             *
             * @return False for a non-positive speed or an invalid path. A leg of zero
             *         duration is a real idiom -- 11% of retail legs have zero length and
             *         zero duration, a "stand here" -- but it comes from a path with no
             *         length, never from a speed of zero, and inventing one from a bad
             *         speed would hide a caller that has lost track of its mover.
             */
            bool Time(const Path& path, float speed);

            bool Valid() const { return !m_marks.empty(); }

            /// One per point of the path. Milliseconds from the leg's start.
            const std::vector<uint32_t>& Marks() const { return m_marks; }

            uint32_t Duration() const { return m_marks.empty() ? 0 : m_marks.back(); }

            float Speed() const { return m_speed; }

            /**
             * @brief Which segment an instant falls in, and how far along it, in [0, 1].
             *
             * Linear in TIME between two marks, which is what the client does. It is not
             * the same as linear in arc length once the marks have been truncated, and
             * the difference is under a millisecond per segment -- the client's error, so
             * it is reproduced rather than corrected. Correcting it would put the server
             * back where it started: right by its own arithmetic and wrong about the
             * thing being drawn.
             */
            void Locate(uint32_t elapsed, size_t& segment, float& fraction) const;

        private:
            std::vector<uint32_t> m_marks;
            float m_speed = 0.0f;
    };

    /**
     * @brief The duration a path will be sent with, without building anything.
     *
     * Asked when deciding whether a leg is worth sending at all, or sizing a timer
     * against one. Same accumulator, so the two can never drift apart.
     */
    uint32_t ClientDuration(const Path& path, float speed);
}

#endif
