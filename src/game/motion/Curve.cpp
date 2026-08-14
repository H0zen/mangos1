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

#include "motion/Curve.h"

#include <algorithm>
#include <cmath>

namespace Helm
{
    namespace
    {
        Geometry::Vector3 Lerp(const Geometry::Vector3& a, const Geometry::Vector3& b,
                               float t)
        {
            return Geometry::Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                     a.z + (b.z - a.z) * t);
        }

        /// The control point for a Catmull-Rom segment, with the ends clamped by
        /// duplication so the curve begins and ends on the path's own points.
        Geometry::Vector3 Control(const Path& path, long index)
        {
            const long last = static_cast<long>(path.PointCount()) - 1;
            const long clamped = std::max<long>(0, std::min(index, last));
            return path.Points()[static_cast<size_t>(clamped)];
        }

        /// Uniform Catmull-Rom, the form the client uses. Tension one half.
        Geometry::Vector3 CatmullRom(const Geometry::Vector3& p0,
                                     const Geometry::Vector3& p1,
                                     const Geometry::Vector3& p2,
                                     const Geometry::Vector3& p3, float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;

            const float w0 = -0.5f * t3 + t2 - 0.5f * t;
            const float w1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
            const float w2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
            const float w3 = 0.5f * t3 - 0.5f * t2;

            return Geometry::Vector3(
                p0.x * w0 + p1.x * w1 + p2.x * w2 + p3.x * w3,
                p0.y * w0 + p1.y * w1 + p2.y * w2 + p3.y * w3,
                p0.z * w0 + p1.z * w1 + p2.z * w2 + p3.z * w3);
        }

        void Clamp(const Path& path, size_t& segment, float& fraction)
        {
            const size_t last = path.SegmentCount() ? path.SegmentCount() - 1 : 0;
            segment = std::min(segment, last);
            fraction = std::max(0.0f, std::min(fraction, 1.0f));
        }
    }

    Geometry::Vector3 PointOn(const Path& path, Curve curve, size_t segment,
                              float fraction)
    {
        if (!path.Valid())
        {
            return Geometry::Vector3();
        }

        Clamp(path, segment, fraction);

        const Geometry::Vector3& a = path.Points()[segment];
        const Geometry::Vector3& b = path.Points()[segment + 1];

        if (curve == Curve::Linear)
        {
            return Lerp(a, b, fraction);
        }

        return CatmullRom(Control(path, static_cast<long>(segment) - 1), a, b,
                          Control(path, static_cast<long>(segment) + 2), fraction);
    }

    Geometry::Vector3 TangentOn(const Path& path, Curve curve, size_t segment,
                                float fraction)
    {
        if (!path.Valid())
        {
            return Geometry::Vector3();
        }

        Clamp(path, segment, fraction);

        Geometry::Vector3 direction;

        if (curve == Curve::Linear)
        {
            const Geometry::Vector3& a = path.Points()[segment];
            const Geometry::Vector3& b = path.Points()[segment + 1];
            direction = Geometry::Vector3(b.x - a.x, b.y - a.y, b.z - a.z);
        }
        else
        {
            // A central difference rather than the analytic derivative: the derivative
            // of the clamped form is discontinuous at the joins, and a facing that
            // flicks at every point of a curved leg is worse than one that is a
            // thousandth of a segment behind.
            const float step = 1.0f / 512.0f;
            const Geometry::Vector3 before =
                PointOn(path, curve, segment, std::max(0.0f, fraction - step));
            const Geometry::Vector3 after =
                PointOn(path, curve, segment, std::min(1.0f, fraction + step));
            direction = Geometry::Vector3(after.x - before.x, after.y - before.y,
                                          after.z - before.z);
        }

        const float length = std::sqrt(direction.x * direction.x +
                                       direction.y * direction.y +
                                       direction.z * direction.z);
        if (length <= 0.0f)
        {
            // Standing still. The caller keeps whatever facing it had; inventing one
            // here would spin a stopped mover to face the x axis.
            return Geometry::Vector3();
        }

        return Geometry::Vector3(direction.x / length, direction.y / length,
                                 direction.z / length);
    }

    float SegmentArc(const Path& path, Curve curve, size_t segment)
    {
        if (!path.Valid())
        {
            return 0.0f;
        }

        if (curve == Curve::Linear)
        {
            return path.SegmentLength(segment);
        }

        float arc = 0.0f;
        Geometry::Vector3 previous = PointOn(path, curve, segment, 0.0f);

        for (int step = 1; step <= kSmoothSteps; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(kSmoothSteps);
            const Geometry::Vector3 here = PointOn(path, curve, segment, t);
            const float dx = here.x - previous.x;
            const float dy = here.y - previous.y;
            const float dz = here.z - previous.z;
            arc += std::sqrt(dx * dx + dy * dy + dz * dz);
            previous = here;
        }

        return arc;
    }
}
