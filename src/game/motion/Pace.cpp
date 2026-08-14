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

#include "motion/Pace.h"

#include <algorithm>
#include <cmath>

namespace Helm
{
    namespace
    {
        /// The accumulator itself, in one place so `Pace::Time` and `ClientDuration`
        /// cannot drift apart. Seeded at one; each segment adds its own millisecond
        /// count and the running total is truncated before the next one is added, which
        /// is why the loss compounds per point rather than once at the end.
        template <typename Emit>
        uint32_t Accumulate(const Path& path, float speed, Emit emit)
        {
            double mark = 1.0;
            emit(static_cast<uint32_t>(mark));

            for (size_t i = 0; i < path.SegmentCount(); ++i)
            {
                mark = std::floor(mark + static_cast<double>(path.SegmentLength(i)) * 1000.0 /
                                             static_cast<double>(speed));
                emit(static_cast<uint32_t>(mark));
            }

            return static_cast<uint32_t>(mark);
        }
    }

    bool Pace::Time(const Path& path, float speed)
    {
        m_marks.clear();
        m_speed = 0.0f;

        if (!path.Valid() || !(speed > 0.0f) || !std::isfinite(speed))
        {
            return false;
        }

        m_speed = speed;
        m_marks.reserve(path.PointCount());
        Accumulate(path, speed, [this](uint32_t mark) { m_marks.push_back(mark); });

        return true;
    }

    void Pace::Locate(uint32_t elapsed, size_t& segment, float& fraction) const
    {
        segment = 0;
        fraction = 0.0f;

        if (m_marks.size() < 2)
        {
            return;
        }

        const uint32_t start = m_marks.front();
        const uint32_t end = m_marks.back();

        if (elapsed <= start)
        {
            return;
        }

        if (elapsed >= end)
        {
            segment = m_marks.size() - 2;
            fraction = 1.0f;
            return;
        }

        // The mark at or before the instant. Binary search rather than a walk: a leg may
        // carry ninety-three points and this is asked every tick for every mover.
        const auto at = std::upper_bound(m_marks.begin(), m_marks.end(), elapsed);
        const size_t index = static_cast<size_t>(at - m_marks.begin());
        segment = index >= 1 ? index - 1 : 0;
        segment = std::min(segment, m_marks.size() - 2);

        const uint32_t from = m_marks[segment];
        const uint32_t to = m_marks[segment + 1];

        // A zero-length segment is not a defect: two points at the same place is how a
        // leg says "turn here". Its fraction is meaningless and one is the answer that
        // moves past it rather than dividing by nothing.
        const float span = static_cast<float>(to - from);
        fraction = to > from ? static_cast<float>(elapsed - from) / span : 1.0f;
    }

    uint32_t ClientDuration(const Path& path, float speed)
    {
        if (!path.Valid() || !(speed > 0.0f) || !std::isfinite(speed))
        {
            return 0;
        }

        return Accumulate(path, speed, [](uint32_t) {});
    }
}
