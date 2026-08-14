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

#include "motion/Motion.h"

#include <cmath>

namespace Helm
{
    namespace
    {
        /// A quarter of a yard, the wire's packing quantum. An interior point of a
        /// packed leg is rounded to this, so the geometry the client draws differs from
        /// the geometry the server planned by at most half a quantum per axis.
        constexpr float kPackQuantum = 0.25f;
    }

    bool Motion::Begin(const Path& path, float speed, Instant at, Curve curve,
                       uint32_t id)
    {
        Clear();

        if (!path.Valid() || !m_pace.Time(path, speed))
        {
            return false;
        }

        m_path = path;
        m_curve = curve;
        m_at = at;
        m_id = id;

        return true;
    }

    void Motion::Clear()
    {
        m_path = Path();
        m_pace = Pace();
        m_curve = Curve::Linear;
        m_at = 0;
        m_id = 0;
        m_lastHeading = 0.0f;
    }

    uint32_t Motion::Elapsed(Instant now) const
    {
        // Unsigned subtraction, so a clock that has wrapped past the leg's start still
        // yields the elapsed time rather than four billion milliseconds.
        const uint32_t since = now - m_at;
        const uint32_t total = Duration();
        return since > total ? total : since;
    }

    Geometry::Vector3 Motion::At(Instant now) const
    {
        if (!Valid())
        {
            return Geometry::Vector3();
        }

        size_t segment = 0;
        float fraction = 0.0f;
        m_pace.Locate(Elapsed(now), segment, fraction);

        return PointOn(m_path, m_curve, segment, fraction);
    }

    float Motion::Heading(Instant now) const
    {
        if (!Valid())
        {
            return m_lastHeading;
        }

        size_t segment = 0;
        float fraction = 0.0f;
        m_pace.Locate(Elapsed(now), segment, fraction);

        const Geometry::Vector3 tangent =
            TangentOn(m_path, m_curve, segment, fraction);

        if (tangent.x == 0.0f && tangent.y == 0.0f)
        {
            // Standing still, or moving straight up. Neither is a facing, and a mover
            // that stops keeps the one it had rather than snapping to the x axis.
            return m_lastHeading;
        }

        m_lastHeading = std::atan2(tangent.y, tangent.x);
        return m_lastHeading;
    }

    Slack Motion::Uncertainty(Instant now, uint32_t clockDriftMs) const
    {
        Slack slack;

        if (!Valid())
        {
            return slack;
        }

        const uint32_t elapsed = Elapsed(now);
        if (elapsed == 0 || elapsed >= Duration())
        {
            // Both ends of a leg are exact: the start is where the mover was when it was
            // sent, and the duration on the wire makes the end agree by construction.
            return slack;
        }

        // The geometry's part. Interior points are packed to a quarter yard on each
        // axis, so the worst placement error is half a quantum in each of three -- and
        // the ends of the leg are not packed at all, which is why this is zero there.
        slack.geometry = kPackQuantum * 0.5f * std::sqrt(3.0f);

        if (m_curve == Curve::CatmullRom)
        {
            // A curved leg is parameterised by index rather than by arc length, or may
            // be; the difference is bounded by how far one segment's arc departs from
            // its chord, and that is the honest bound to report while it is unmeasured.
            size_t segment = 0;
            float fraction = 0.0f;
            m_pace.Locate(elapsed, segment, fraction);

            const float arc = SegmentArc(m_path, m_curve, segment);
            const float chord = m_path.SegmentLength(segment);
            slack.geometry += arc > chord ? arc - chord : 0.0f;
        }

        // The clock's part, in yards: what the client says it is behind by, at this
        // leg's speed. Told to us rather than guessed -- see CMSG_MOVE_TIME_SKIPPED.
        slack.clock = static_cast<float>(clockDriftMs) * 0.001f * m_pace.Speed();

        return slack;
    }
}
