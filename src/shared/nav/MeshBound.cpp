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

#include "nav/MeshBound.hpp"

#include <algorithm>
#include <cmath>

namespace Nav
{
    namespace
    {
        float Dist(float ax, float ay, float bx, float by)
        {
            const float dx = ax - bx;
            const float dy = ay - by;
            return std::sqrt(dx * dx + dy * dy);
        }

        /// `|s - p| + |p - t|` at one point.
        float Through(float sx, float sy, float tx, float ty, float px, float py)
        {
            return Dist(sx, sy, px, py) + Dist(px, py, tx, ty);
        }

        /**
         * @brief The minimum of `|s - p| + |p - t|` over one segment, exactly.
         *
         * BY REFLECTION, which turns a sum of two distances into a single one. Mirror
         * `t` across the segment's line; for any `p` ON that line `|p - t|` equals
         * `|p - t'|`, so the sum is the length of a bent path `s -> p -> t'` and is
         * shortest where it is not bent at all -- where the straight segment `s..t'`
         * crosses the line.
         *
         * When `s` and `t` are already on opposite sides the reflection is unnecessary:
         * the straight line between them crosses the segment's line by itself, and the
         * minimum is `|s - t|` there.
         *
         * Either way the crossing may fall outside the segment. The function restricted
         * to the line is convex with a single minimum, so the constrained answer is then
         * whichever end is nearer to it -- the clamp is exact, not an approximation.
         */
        float MinOverSegment(float sx, float sy, float tx, float ty,
                             float ax, float ay, float bx, float by)
        {
            const float ex = bx - ax;
            const float ey = by - ay;
            const float len2 = ex * ex + ey * ey;
            if (len2 <= 0.0f)
            {
                return Through(sx, sy, tx, ty, ax, ay);
            }

            // Signed areas: which side of the segment's line each endpoint is on.
            const float sideS = (sx - ax) * ey - (sy - ay) * ex;
            const float sideT = (tx - ax) * ey - (ty - ay) * ex;

            float gx = tx;
            float gy = ty;
            if (sideS * sideT > 0.0f)
            {
                // Same side: mirror the target across the line.
                const float d = ((tx - ax) * ex + (ty - ay) * ey) / len2;
                const float footX = ax + ex * d;
                const float footY = ay + ey * d;
                gx = 2.0f * footX - tx;
                gy = 2.0f * footY - ty;
            }

            // Where the straight run from s to the (possibly mirrored) target crosses
            // the line, as a parameter along the segment.
            const float rx = gx - sx;
            const float ry = gy - sy;
            const float denom = rx * ey - ry * ex;

            float u = 0.0f;
            if (std::fabs(denom) > 1e-12f)
            {
                u = ((sx - ax) * ry - (sy - ay) * rx) / -denom;
            }
            else
            {
                // Parallel: the sum varies monotonically along the segment, so the
                // projection of s is as good a starting guess as any and the clamp
                // below settles it.
                u = ((sx - ax) * ex + (sy - ay) * ey) / len2;
            }

            u = std::max(0.0f, std::min(1.0f, u));

            return Through(sx, sy, tx, ty, ax + ex * u, ay + ey * u);
        }

        /// Does the segment `s..t` meet the closed box at all?
        bool SegmentMeetsBox(float sx, float sy, float tx, float ty,
                             float minX, float minY, float maxX, float maxY)
        {
            // Liang-Barsky. A parametric clip: each slab shrinks the surviving interval
            // of the segment, and anything left at the end is inside.
            float lo = 0.0f;
            float hi = 1.0f;
            const float dx = tx - sx;
            const float dy = ty - sy;

            const float p[4] = { -dx, dx, -dy, dy };
            const float q[4] = { sx - minX, maxX - sx, sy - minY, maxY - sy };

            for (int i = 0; i < 4; ++i)
            {
                if (std::fabs(p[i]) < 1e-12f)
                {
                    if (q[i] < 0.0f)
                    {
                        return false;   // parallel to this slab and outside it
                    }
                    continue;
                }

                const float r = q[i] / p[i];
                if (p[i] < 0.0f)
                {
                    lo = std::max(lo, r);
                }
                else
                {
                    hi = std::min(hi, r);
                }
                if (lo > hi)
                {
                    return false;
                }
            }
            return true;
        }
    }

    float BoxDetourLength(float sx, float sy, float tx, float ty,
                          float minX, float minY, float maxX, float maxY)
    {
        // Either end already inside, or the straight run passes through: no detour at
        // all, and the bound is the straight distance. Checked first because it is both
        // the common case and the cheap one.
        if (SegmentMeetsBox(sx, sy, tx, ty, minX, minY, maxX, maxY))
        {
            return Dist(sx, sy, tx, ty);
        }

        // Outside and off the line, so the best point is on the boundary: `f` is convex
        // and its unconstrained minimum -- anywhere on the segment s..t -- is not in the
        // box, so the constrained one is on the box's edge.
        float best = MinOverSegment(sx, sy, tx, ty, minX, minY, maxX, minY);
        best = std::min(best,
                        MinOverSegment(sx, sy, tx, ty, maxX, minY, maxX, maxY));
        best = std::min(best,
                        MinOverSegment(sx, sy, tx, ty, maxX, maxY, minX, maxY));
        best = std::min(best,
                        MinOverSegment(sx, sy, tx, ty, minX, maxY, minX, minY));
        return best;
    }

    bool BoxCanCarryPath(float sx, float sy, float tx, float ty,
                         float minX, float minY, float maxX, float maxY,
                         float upper)
    {
        if (!(upper > 0.0f))
        {
            return true;   // no route in hand, so nothing may be ruled out
        }

        // The slack is not politeness. The bound is float arithmetic over four square
        // roots and the upper bound came out of a different summation of the same
        // world; discarding an area that ties with the best route would lose the answer
        // for the sake of one expansion.
        const float SLACK = 1e-3f;
        return BoxDetourLength(sx, sy, tx, ty, minX, minY, maxX, maxY)
               <= upper + SLACK;
    }
}
