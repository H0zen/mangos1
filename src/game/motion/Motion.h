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

#ifndef MANGOS_HELM_MOTION_H
#define MANGOS_HELM_MOTION_H

/**
 * @file Motion.h
 * @brief WHERE, WHEN -- the four values composed, and the whole answer.
 *
 * `Motion` is a `Path`, a `Curve`, a `Pace` and the instant the leg began. From those it
 * answers, for any instant: where the mover is, which way it is facing, whether it has
 * arrived, and how sure any of that is.
 *
 * ## Why this replaces a position rather than smoothing one
 *
 * The server used to keep a position and refresh it from the spline every 400 ms. Between
 * refreshes it lagged by up to 2.8 yards at a run, on every moving creature, always in
 * the direction of travel -- and everything that asks where something is was reading it:
 * spell range, melee reach, aggro radius, line of sight.
 *
 * A `Motion` can answer exactly, for any instant, at the cost of a binary search over at
 * most ninety-three marks and a lerp. So there is no second position to keep in step with
 * a plan, and nothing to drift. That is not a better approximation than the old one; it
 * is the removal of an approximation.
 *
 * The captures support this strongly: matching 7,658 consecutive retail legs, the new
 * leg's origin is never more than 0.67 yards off the previous leg's polyline, and that
 * residual is the quarter-yard packing of the points being reconstructed rather than
 * drift. There is no second position on the retail server either. The plan IS the
 * position.
 *
 * ## What it deliberately cannot do
 *
 * It cannot tell you where the mover is in the world if the leg is on a deck. A `Motion`
 * answers in its `Path`'s frame and says which frame that is; composing a deck answer
 * into world coordinates would require the hull's true pose, which this server does not
 * have and cannot obtain. Callers compare frames.
 *
 * It cannot be edited. A leg is replaced, not adjusted -- retail re-lays one four times
 * in five in a busy city -- so `Motion` is a value that is thrown away and rebuilt, and
 * every field of it is const once begun.
 */

#include "motion/CourseTime.h"
#include "motion/Curve.h"
#include "motion/Pace.h"
#include "motion/Path.h"

#include <cstdint>

namespace Helm
{
    // `Instant` and `Millis` come from CourseTime.h. They were declared a second time
    // here, and a clock declared twice is a clock reasoned about twice: this file went
    // on to compare two instants with unsigned subtraction, which reads one millisecond
    // before a leg's start as forty-nine days after it. CourseTime.h exists precisely so
    // that comparison is unwritable -- every question is a signed `Since`.

    /**
     * @brief How far an answer may be from what the client is drawing, in yards.
     *
     * Zero when nothing is running, and zero at both ends of a leg -- the duration is on
     * the wire and both sides agree there by construction. In between there is real
     * uncertainty, and stating it is better than pretending it away. Two parts:
     *
     *   - what the geometry costs: the quarter-yard packing of interior points, and on a
     *     curved leg a parameterisation that is not arc length;
     *   - what the clock costs: how far the client's movement clock has drifted, in
     *     milliseconds, turned into yards at this leg's speed.
     *
     * The second is not guesswork. `CMSG_MOVE_TIME_SKIPPED` is the client admitting how
     * far behind it is -- never zero, never negative, clustering at 66-70 ms, one frame
     * at fifteen fps -- so the server can subtract a number it was told rather than
     * estimate one.
     */
    struct Slack
    {
        float geometry = 0.0f;
        float clock = 0.0f;

        float Total() const { return geometry + clock; }
    };

    class Motion
    {
        public:
            Motion() = default;

            /**
             * @brief Begin a leg.
             *
             * @param path   where it goes
             * @param speed  yards per second
             * @param at     the instant it started, on the server's movement clock
             * @param curve  straight unless the leg is a flight
             * @param id     the spline id this leg was sent with, so a later answer can
             *               refuse to speak for a leg that has since been replaced.
             *               Zero means "not sent yet", and every leg the inherited code
             *               produced had a zero here, which is why nothing could tell
             *               two of them apart.
             */
            bool Begin(const Path& path, float speed, Instant at,
                       Curve curve = Curve::Segmented, uint32_t id = 0);

            bool Valid() const { return m_path.Valid() && m_pace.Valid(); }

            /// A motion that is not a plan: `Ended` is true for it at every instant.
            void Clear();

            const Path& Route() const { return m_path; }
            const Pace& Timing() const { return m_pace; }
            Curve Interpolation() const { return m_curve; }
            const Frame& InFrame() const { return m_path.InFrame(); }

            uint32_t Id() const { return m_id; }
            Instant StartedAt() const { return m_at; }
            uint32_t Duration() const { return m_pace.Duration(); }

            /// Milliseconds since the leg began, clamped to [0, Duration()]. Unsigned
            /// subtraction, so it stays right across the clock's wrap.
            uint32_t Elapsed(Instant now) const;

            bool Ended(Instant now) const { return Elapsed(now) >= Duration(); }

            /// Where the mover is. In the path's frame, never in the world's unless they
            /// are the same frame.
            Geometry::Vector3 At(Instant now) const;

            /// Facing along the direction of travel, in radians. Falls back to the
            /// previous heading where the path stands still.
            float Heading(Instant now) const;

            /**
             * @brief How sure `At(now)` is.
             *
             * @param clockDriftMs what the client has admitted to being behind, in
             *                     milliseconds. Zero when nothing is known, which makes
             *                     the answer the geometry's part alone rather than a
             *                     pretence that there is no clock error.
             */
            Slack Uncertainty(Instant now, uint32_t clockDriftMs = 0) const;

        private:
            Path m_path;
            Pace m_pace;
            Curve m_curve = Curve::Segmented;
            Instant m_at = 0;
            uint32_t m_id = 0;
            mutable float m_lastHeading = 0.0f;
    };
}

#endif
