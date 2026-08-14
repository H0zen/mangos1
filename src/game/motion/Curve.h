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

#ifndef MANGOS_HELM_CURVE_H
#define MANGOS_HELM_CURVE_H

/**
 * @file Curve.h
 * @brief HOW A PATH BENDS BETWEEN ITS POINTS.
 *
 * The third value, and the smallest: a pure function of a `Path`, a segment and a
 * fraction. It holds no state, so it is not a class -- making it one would suggest there
 * is something to configure, and there is not. There are two interpolations on this wire
 * and the client picks between them with one bit.
 *
 * ## Straight, or Catmull-Rom, and the choice is not ours
 *
 * One wire flag selects Catmull-Rom, and the SAME flag selects the flying animation. In
 * four retail captures, 48 legs used the curved encoding and every one of them had that
 * flag; none of the 32,758 straight-encoded legs did. So there is no such thing as a
 * curved walk in this format. A ground creature's route is a polyline, and "smoothing"
 * one is not a quality improvement -- it is sending a leg the client will animate as
 * flight.
 *
 * That is why waypoint smoothing in this tree welds nodes into a LINEAR spline rather
 * than fitting a curve through them.
 *
 * ## The ends
 *
 * Catmull-Rom needs a control point either side of the segment it is drawing, and the
 * first and last segments have only one. The ends are duplicated -- the standard clamp
 * -- which makes the curve begin and end exactly on the points it was given. Any other
 * choice starts the mover slightly off its own start position, and the start position is
 * the one point on a leg that both sides already agree about.
 */

#include "motion/Path.h"

#include <cstdint>

namespace Helm
{
    enum class Curve : uint8_t
    {
        /// Straight segments. Everything that walks, runs or swims.
        Linear = 0,

        /// Catmull-Rom through the points. Only ever seen together with flight.
        CatmullRom = 1,
    };

    /**
     * @brief The position a fraction of the way along one segment.
     *
     * @param path     the geometry
     * @param curve    which interpolation
     * @param segment  index of the segment, clamped into range
     * @param fraction position within it, clamped to [0, 1]
     */
    Geometry::Vector3 PointOn(const Path& path, Curve curve, size_t segment,
                              float fraction);

    /**
     * @brief The direction of travel there, as a unit vector.
     *
     * Taken from the curve rather than from the segment's endpoints, so a Catmull-Rom
     * leg faces along its bend instead of along the chord it is bending away from. Zero
     * length where the path stands still, which the caller must treat as "keep the
     * facing you had": a mover that stops does not spin to face north.
     */
    Geometry::Vector3 TangentOn(const Path& path, Curve curve, size_t segment,
                                float fraction);

    /**
     * @brief Arc length of one segment under a curve, in yards.
     *
     * Equal to the straight distance for `Linear`. For `CatmullRom` it is estimated by
     * subdivision, because a Catmull-Rom arc has no closed form -- see `kSmoothSteps`
     * for what that costs and what it is not known to buy.
     */
    float SegmentArc(const Path& path, Curve curve, size_t segment);

    /**
     * @brief Subdivisions per segment when measuring a Catmull-Rom arc.
     *
     * The one constant in this layer with no measurement behind it. Timestamp-less
     * captures cannot show whether the client divides a leg's duration between segments
     * by arc length or by index, and the two differ only in the middle of a CURVED leg
     * -- 48 legs of 32,806. The endpoints are exact either way, and
     * `SMSG_FLIGHT_SPLINE_SYNC` exists to correct the middle. Written down as unknown
     * rather than tuned until it looked right.
     */
    constexpr int kSmoothSteps = 3;
}

#endif
