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

#include "motion/Path.h"

#include <cmath>

namespace Helm
{
    namespace
    {
        bool Finite(const Geometry::Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        float Distance(const Geometry::Vector3& a, const Geometry::Vector3& b)
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float dz = b.z - a.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    bool Path::Build(const std::vector<Geometry::Vector3>& points, Frame frame)
    {
        m_points.clear();
        m_lengths.clear();
        m_length = 0.0f;
        m_frame = frame;

        if (points.size() < 2)
        {
            return false;
        }

        for (const Geometry::Vector3& point : points)
        {
            if (!Finite(point))
            {
                // Refused here and nowhere else. A NaN that gets past this point
                // reappears as a distance, a duration, a packed offset and a position,
                // and by then nothing can say where it came from.
                m_points.clear();
                return false;
            }
        }

        m_points = points;
        m_lengths.reserve(m_points.size() - 1);

        for (size_t i = 0; i + 1 < m_points.size(); ++i)
        {
            const float length = Distance(m_points[i], m_points[i + 1]);
            m_lengths.push_back(length);
            m_length += length;
        }

        return true;
    }

    float Path::Chord() const
    {
        if (m_points.size() < 2)
        {
            return 0.0f;
        }
        return Distance(m_points.front(), m_points.back());
    }

    Geometry::Vector3 Path::AtDistance(float fraction) const
    {
        if (m_points.empty())
        {
            return Geometry::Vector3();
        }
        if (m_points.size() == 1 || m_length <= 0.0f)
        {
            return m_points.front();
        }

        if (fraction <= 0.0f)
        {
            return m_points.front();
        }
        if (fraction >= 1.0f)
        {
            return m_points.back();
        }

        float want = fraction * m_length;
        for (size_t i = 0; i < m_lengths.size(); ++i)
        {
            if (want > m_lengths[i])
            {
                want -= m_lengths[i];
                continue;
            }

            const float t = m_lengths[i] > 0.0f ? want / m_lengths[i] : 0.0f;
            const Geometry::Vector3& a = m_points[i];
            const Geometry::Vector3& b = m_points[i + 1];
            return Geometry::Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                     a.z + (b.z - a.z) * t);
        }

        return m_points.back();
    }
}
