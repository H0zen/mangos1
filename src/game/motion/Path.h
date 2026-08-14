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

#ifndef MANGOS_HELM_PATH_H
#define MANGOS_HELM_PATH_H

/**
 * @file Path.h
 * @brief WHERE, AND NOTHING ELSE.
 *
 * The first of four values that make up a movement, and the only one that is pure
 * geometry. It knows a list of points and the frame they are in. It does not know how
 * fast anything travels along it, when it started, what is travelling, or what any of it
 * will be told to a client.
 *
 * ## The four values
 *
 * A movement in this design is a composition of four things, each of which can be built,
 * printed and asserted on with nothing else present:
 *
 *   Path     where          points in a frame                     -- this file
 *   Curve    how it bends   straight, or Catmull-Rom through them  -- Curve.h
 *   Pace     how fast       speed -> the client's own timing       -- Pace.h
 *   Motion   where WHEN     Path + Curve + Pace + a start instant  -- Motion.h
 *
 * Nothing in that list refers to a `Unit`, a `Map`, a packet or a clock the process
 * happens to be running on. That is the whole design: the thing that decides a creature
 * should move somewhere, the thing that answers where it is, and the thing that tells a
 * client about it are three different problems, and only the first of them needs to know
 * what a creature is. The inherited `movement/` code fused all three into one object that
 * could not be evaluated without knowing which packet had produced it, and could not be
 * tested without standing up a world.
 *
 * ## The frame is part of the geometry, not a note attached to it
 *
 * A path on a ship's deck is expressed in that ship's own map, and a path in the world is
 * expressed in the world's. They are never composed: the server's idea of where a hull is
 * comes from a waypoint estimate the client does not share, so anything built by
 * multiplying a deck offset by a hull pose is fiction. A `Path` therefore carries its
 * frame, and every consumer compares frames before it compares positions.
 */

#include "Geometry/Vector3.h"

#include <cstdint>
#include <vector>

namespace Helm
{
    /**
     * @brief Which coordinate system a path's numbers mean.
     *
     * A map id, and for a vessel the id of the map its hull was baked as. Two paths in
     * different frames have nothing to say to each other -- not "roughly the same place",
     * nothing -- and the comparison exists so that saying so is one line at every call
     * site instead of a comment nobody reads.
     */
    struct Frame
    {
        uint32_t map = 0;

        bool operator==(const Frame& o) const { return map == o.map; }
        bool operator!=(const Frame& o) const { return !(*this == o); }
    };

    /**
     * @brief A route as geometry: points, in a frame, and the lengths between them.
     *
     * Immutable once built. Retail replaces a leg four times in five rather than editing
     * one, so a mutable path buys nothing and costs the guarantee that whoever is
     * evaluating it is looking at what they were handed.
     */
    class Path
    {
        public:
            Path() = default;

            /**
             * @brief Build from points in a frame.
             *
             * @return False for fewer than two points, or for any non-finite coordinate.
             *         Both are refused at the door rather than propagated: a NaN in a
             *         position spreads to every distance, every comparison and every
             *         packet built from it, and the place it entered is the only place
             *         it can still be named.
             */
            bool Build(const std::vector<Geometry::Vector3>& points, Frame frame);

            bool Valid() const { return m_points.size() >= 2; }

            const Frame& InFrame() const { return m_frame; }

            const std::vector<Geometry::Vector3>& Points() const { return m_points; }

            size_t PointCount() const { return m_points.size(); }
            size_t SegmentCount() const
            {
                return m_points.empty() ? 0 : m_points.size() - 1;
            }

            /// Length of one segment, in yards, in three dimensions.
            float SegmentLength(size_t segment) const
            {
                return segment < m_lengths.size() ? m_lengths[segment] : 0.0f;
            }

            /**
             * @brief Total length along the points, in three dimensions.
             *
             * The POLYLINE, not the chord between the ends. Multi-point retail legs imply
             * 2.529 yd/s measured along the polyline against 2.435 along the chord, and
             * 2.5 is the speed that was actually sent -- so the polyline is what the
             * client is timing.
             *
             * And three dimensions, not two. The residual against the sent duration is
             * 0.96 ms at the 95th percentile in three, 28 ms in two on a coastal route,
             * and the two collapse together in a dungeon where the floors are flat --
             * which is what must happen if Z genuinely counts, and it does.
             */
            float Length() const { return m_length; }

            /// Straight-line distance between the two ends. Only ever a diagnostic: no
            /// timing in this design is derived from it.
            float Chord() const;

            /// The point at a fraction of the LENGTH along the polyline, in [0, 1].
            /// Arc-length parameterised, which is not how a leg is timed -- see Pace --
            /// and is what a caller sampling a shape wants.
            Geometry::Vector3 AtDistance(float fraction) const;

        private:
            std::vector<Geometry::Vector3> m_points;
            std::vector<float> m_lengths;
            Frame m_frame;
            float m_length = 0.0f;
    };
}

#endif
