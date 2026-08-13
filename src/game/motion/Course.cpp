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

#include "Course.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float TWO_PI_F = 2.0f * 3.14159265358979323846f;

    /**
     * @brief Subdivisions used to measure the arc length of one Catmull-Rom segment.
     *
     * A curved segment is longer than its chord, and the client charges the difference
     * in time, so the length has to be measured the same way at both ends or a flight
     * arrives early. Measuring it means walking the curve; this is how finely.
     *
     * It matters ONLY for Curve::Smooth, which in this wire format means flight paths
     * and nothing else -- 42 legs out of the 11,518 in the retail captures. On a
     * straight segment every subdivision count gives the same answer, which is why the
     * ground case is unaffected by the choice.
     */
    constexpr int kSmoothSteps = 3;

    /**
     * @brief The millisecond every course starts one ahead by.
     *
     * Not a fudge: it is in the wire timing. On 2,057 single-hop legs across three
     * retail captures the duration exceeds length/speed by a value uniformly
     * distributed over (0, 1] ms -- median 0.44, 0.44 and 0.53 in the three files.
     * That is the signature of an accumulator that starts at 1 and truncates, and
     * reproducing it is what makes a server-side course agree with the client to the
     * millisecond rather than to the tick.
     */
    constexpr uint32 kOriginMark = 1;

    /**
     * @brief Excess path length the wire's 0.25-yard quantisation introduces.
     *
     * Interior points travel as quantised offsets, so the polyline the client walks
     * zig-zags very slightly around the one that was planned, and is therefore longer.
     * Reconstructing retail's own multi-point legs from the wire and dividing by the
     * duration gives 2.529 yd/s where 2.500 was intended: 1.16% long.
     */
    constexpr float kQuantisationExcess = 0.012f;

    /// Uniform Catmull-Rom at parameter `t`, the client's basis.
    Helm::Vector3 CatmullRom(Helm::Vector3 const c[4], float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float w0 = -0.5f * t3 + 1.0f * t2 - 0.5f * t;
        const float w1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
        const float w2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
        const float w3 = 0.5f * t3 - 0.5f * t2;
        return c[0] * w0 + c[1] * w1 + c[2] * w2 + c[3] * w3;
    }

    /// Derivative of the same basis.
    Helm::Vector3 CatmullRomPrime(Helm::Vector3 const c[4], float t)
    {
        const float t2 = t * t;
        const float w0 = -1.5f * t2 + 2.0f * t - 0.5f;
        const float w1 = 4.5f * t2 - 5.0f * t;
        const float w2 = -4.5f * t2 + 4.0f * t + 0.5f;
        const float w3 = 1.5f * t2 - 1.0f * t;
        return c[0] * w0 + c[1] * w1 + c[2] * w2 + c[3] * w3;
    }

    float Wrap2Pi(float a)
    {
        while (a < 0.0f)
        {
            a += TWO_PI_F;
        }
        while (a >= TWO_PI_F)
        {
            a -= TWO_PI_F;
        }
        return a;
    }
}

namespace Helm
{
    void Course::Controls(size_t k, Vector3 out[4]) const
    {
        const size_t last = m_points.size() - 1;

        // The client manufactures the two missing endpoints rather than clamping: the
        // head is reflected through the first point and the tail is duplicated. Getting
        // this wrong bends the first and last segments away from where the client draws
        // them, which is exactly where a landing looks wrong.
        out[0] = (k == 0) ? (m_points[0] * 2.0f - m_points[1]) : m_points[k - 1];
        out[1] = m_points[k];
        out[2] = m_points[k + 1];
        out[3] = (k + 2 <= last) ? m_points[k + 2] : m_points[last];
    }

    Course Course::Plan(Domain const& domain, std::vector<Vector3> points, Gait gait,
                        float speed, Facing const& facing, Curve curve, Instant at,
                        uint32 id)
    {
        Course c;
        if (points.size() < 2 || !(speed > 0.0f))
        {
            return c;
        }

        c.m_domain = domain;
        c.m_points = std::move(points);
        c.m_gait = gait;
        c.m_speed = speed;
        c.m_facing = facing;
        c.m_curve = curve;
        c.m_at = at;
        c.m_id = id;
        c.m_quantised = c.m_points.size() > 2;

        // A curved course is a flying course; the wire has one bit for both and no way
        // to say otherwise. Rather than let a caller believe it asked for a smooth walk
        // and get a flight, the contradiction is resolved here, once, in the open.
        if (c.m_curve == Curve::Smooth)
        {
            c.m_gait = Gait::Fly;
        }

        // === The timing. This is the client's arithmetic, not ours. ===
        //
        // A single float accumulator, seeded at one millisecond, truncated to an
        // integer after every segment. Both quirks are visible in the retail captures
        // and both matter: the seed puts every duration one millisecond long, and the
        // per-segment truncation loses up to a millisecond per point, so a 46-point leg
        // (the longest observed) can run 45 ms short of the naive length/speed. A
        // server that computes the "correct" duration instead disagrees with the client
        // by that much on every leg, and the disagreement is what makes a unit arrive
        // somewhere the client has not reached.
        const float perYard = 1000.0f / speed;
        const size_t n = c.m_points.size();
        c.m_marks.assign(n, 0);
        uint32 mark = kOriginMark;
        for (size_t k = 1; k < n; ++k)
        {
            float length;
            if (c.m_curve == Curve::Smooth)
            {
                Vector3 ctrl[4];
                c.Controls(k - 1, ctrl);
                length = 0.0f;
                Vector3 prev = CatmullRom(ctrl, 0.0f);
                for (int s = 1; s <= kSmoothSteps; ++s)
                {
                    const Vector3 cur =
                        CatmullRom(ctrl, float(s) / float(kSmoothSteps));
                    length += (cur - prev).length();
                    prev = cur;
                }
            }
            else
            {
                length = (c.m_points[k] - c.m_points[k - 1]).length();
            }

            // TWO STATEMENTS, AND THEY MUST STAY TWO. Folded into one expression,
            // `float(mark) + length * perYard` is something the compiler may contract
            // into a fused multiply-add: one rounding where the spline beside us does
            // two. The spline cannot be contracted the same way because its segment
            // length arrives through a member-function pointer, which nothing inlines.
            //
            // The difference is one unit in the last place, and it does not matter
            // until the true value sits within one of a whole millisecond -- at which
            // point the truncation below lands on a different integer. That is what
            // the live canary caught: a handful of legs out of many, off by exactly
            // one millisecond, in BOTH directions.
            //
            // Rounding the product on its own removes the larger of the two places
            // this can happen. The other is inside length(), which is shared code and
            // inlined here but not there, so a millisecond of disagreement remains
            // possible and the canary is calibrated for it rather than against it.
            const float step = length * perYard;
            mark = uint32(float(mark) + step);
            c.m_marks[k] = mark;
        }

        // Every point at the same coordinate. Nothing to travel, so the course is not
        // one -- returning it with a zero duration would make Ended() true at the
        // instant it was laid and leave the caller looping.
        if (c.m_marks.back() <= kOriginMark)
        {
            return Course();
        }

        return c;
    }

    uint32 Course::Elapsed(Instant now) const
    {
        if (Empty())
        {
            return 0;
        }
        const Millis since = Since(now, m_at);
        if (since <= 0)
        {
            return 0;
        }
        const uint32 total = Duration();
        return (uint32(since) > total) ? total : uint32(since);
    }

    float Course::Progress(Instant now) const
    {
        const uint32 total = Duration();
        if (!total)
        {
            return 0.0f;
        }
        return float(Elapsed(now)) / float(total);
    }

    void Course::Locate(uint32 elapsed, size_t& segment, float& u) const
    {
        // The last mark that is not past `elapsed`. upper_bound lands one beyond it,
        // and the marks are strictly increasing except where two points coincide, so
        // the step back is always to a real segment.
        auto it = std::upper_bound(m_marks.begin(), m_marks.end(), elapsed);
        size_t k = size_t(it - m_marks.begin());
        if (k == 0)
        {
            k = 1;
        }
        if (k >= m_marks.size())
        {
            k = m_marks.size() - 1;
        }
        segment = k - 1;

        const uint32 lo = m_marks[k - 1];
        const uint32 hi = m_marks[k];
        u = (hi > lo) ? float(elapsed - lo) / float(hi - lo) : 1.0f;
        u = std::min(1.0f, std::max(0.0f, u));
    }

    Vector3 Course::At(Instant now) const
    {
        if (Empty())
        {
            return Vector3();
        }

        size_t k = 0;
        float u = 0.0f;
        Locate(Elapsed(now), k, u);

        if (m_curve == Curve::Smooth)
        {
            Vector3 ctrl[4];
            Controls(k, ctrl);
            return CatmullRom(ctrl, u);
        }
        return m_points[k] + (m_points[k + 1] - m_points[k]) * u;
    }

    float Course::Heading(Instant now) const
    {
        if (Empty())
        {
            return 0.0f;
        }

        // Once the course is over the client holds whatever final facing it was given,
        // so that is what the server must report -- reading the last segment's
        // direction instead would have the unit facing its arrival vector while the
        // client has it looking at a player.
        if (Ended(now))
        {
            switch (m_facing.mode)
            {
                case Facing::Mode::Angle:
                    return Wrap2Pi(m_facing.angle);
                case Facing::Mode::Spot:
                {
                    const Vector3 here = m_points.back();
                    return Wrap2Pi(std::atan2(m_facing.spot.y - here.y,
                                              m_facing.spot.x - here.x));
                }
                case Facing::Mode::Target:
                    // The target moves; only the caller knows where it is now. The
                    // arrival heading is the honest answer here.
                    break;
                case Facing::Mode::Travel:
                    break;
            }
        }

        size_t k = 0;
        float u = 0.0f;
        Locate(Elapsed(now), k, u);

        Vector3 d;
        if (m_curve == Curve::Smooth)
        {
            Vector3 ctrl[4];
            Controls(k, ctrl);
            d = CatmullRomPrime(ctrl, u);
        }
        else
        {
            d = m_points[k + 1] - m_points[k];
        }
        return Wrap2Pi(std::atan2(d.y, d.x));
    }

    float Course::Slack(Instant now) const
    {
        if (Empty())
        {
            return 0.0f;
        }

        const uint32 total = Duration();
        const uint32 elapsed = Elapsed(now);
        if (!elapsed || elapsed >= total)
        {
            return 0.0f;   // The ends are exact. That is the guarantee.
        }

        // Distance is proportional to time here, because the marks were built from
        // length: the same proportionality that makes the wire's single duration field
        // sufficient.
        const float travelled = m_speed * float(elapsed) / 1000.0f;
        const float remaining = m_speed * float(total - elapsed) / 1000.0f;

        float slack = 0.0f;

        // Quantisation: the discrepancy grows with distance covered but must be gone by
        // the end, since both sides arrive at the same point at the same instant.
        if (m_quantised)
        {
            slack += kQuantisationExcess * std::min(travelled, remaining);
        }

        // Parameterisation: within one Catmull-Rom segment the client's parameter is
        // not arc length, so it may lead or lag by part of a segment. A straight
        // segment has no such freedom.
        if (m_curve == Curve::Smooth)
        {
            size_t k = 0;
            float u = 0.0f;
            Locate(elapsed, k, u);
            const float seg = (m_points[k + 1] - m_points[k]).length();
            slack += 0.5f * seg * (4.0f * u * (1.0f - u));
        }

        return slack;
    }
}
