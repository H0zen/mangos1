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

#ifndef MANGOS_COURSE_H
#define MANGOS_COURSE_H

// The PLAN a unit is executing, as a value. This header owes nothing to the world: a
// vector of points, a clock and a guid. That is deliberate -- the whole reason a course
// is a value is that its arithmetic can be asserted on in a test that links none of the
// server, and the arithmetic is the part that has to be exactly right.

#include "CourseTime.h"
#include "Geometry/Vector3.h"
#include "Platform/Define.h"

#include <vector>

namespace Helm
{
    using Geometry::Vector3;

    /**
     * @brief A unit's identity, as the 64-bit value its guid serialises to.
     *
     * Deliberately not ObjectGuid. That type lives in the game library, and a course
     * that included it would drag the server in behind it -- which would cost exactly
     * the property this file exists to have. A course CARRIES identities (the vessel
     * whose deck it runs on, the unit it ends up facing) and never interprets one; the
     * wire writer, which is game code anyway, converts at the boundary.
     */
    using ActorId = uint64;

    /// No unit.
    constexpr ActorId kNoActor = 0;

    /**
     * @brief The pace a leg is travelled at, as a first-class property.
     *
     * Not a flag hidden inside the packet builder. The retail captures settle this: on
     * 32,806 monster-move legs the legs WITHOUT the run bit imply 2.5 yd/s -- the walk
     * speed, and 10,540 of them land on that exact value -- while the legs WITH it
     * imply 7 to 10. Retail leaves the bit clear on 29% to 61% of legs depending on the
     * zone. It is a real, observable distinction and the client animates from it.
     */
    enum class Gait : uint8
    {
        Walk,   ///< Ground, walking animation.
        Run,    ///< Ground, running animation.
        Swim,   ///< In water. Ground interpolation, swim speed.
        Fly     ///< Airborne. THE ONLY GAIT THAT MAY USE Curve::Smooth (see below).
    };

    /**
     * @brief How the client joins the points.
     *
     * Smooth is not a free choice. In this wire format one bit selects BOTH the
     * Catmull-Rom interpolation and the flying animation, so a smooth course is a
     * flying course and there is no such thing as a curved walk. Verified, not assumed:
     * across the three captures every one of the 42 legs sent with absolute point
     * arrays carried that bit, and not one of the 11,476 packed-offset legs did.
     */
    enum class Curve : uint8
    {
        Segmented,  ///< Straight between consecutive points (the ordinary case).
        Smooth      ///< Catmull-Rom. Implies Gait::Fly.
    };

    /**
     * @brief The coordinate world a course lives in.
     *
     * A vessel's deck IS a map. When `vessel` is set, every point in the course is in
     * that vessel's frame and NOTHING may compose it into the world -- the retail
     * captures show the server doing exactly this, sending deck-local coordinates in
     * the range x[-45, 37] y[-9, 11] z[2, 22] for two different ships and deriving the
     * leg duration from the length measured in that frame (2.517 and 2.520 yd/s, the
     * walk speed, on 1,335 legs). Two courses may only be compared when their domains
     * are equal.
     */
    struct Domain
    {
        uint32  map = 0;
        ActorId vessel = kNoActor;   ///< kNoActor for the world; a vessel for a deck.

        bool operator==(Domain const& o) const
        {
            return map == o.map && vessel == o.vessel;
        }

        bool operator!=(Domain const& o) const { return !(*this == o); }

        bool OnDeck() const { return vessel != kNoActor; }
    };

    /**
     * @brief The orientation a course ends in.
     *
     * The client faces the direction of travel while a course runs and applies this
     * when it ends, so "face the player" is not an action -- it is a property of a
     * course. The wire carries it as a type byte plus a payload, entirely separate from
     * the flags, which is why facing is orthogonal to everything else here.
     */
    struct Facing
    {
        enum class Mode : uint8
        {
            Travel,  ///< Keep the direction of travel (the default).
            Angle,   ///< Hold a fixed heading, radians.
            Target,  ///< Point at a unit.
            Spot     ///< Face a fixed point.
        };

        Mode    mode = Mode::Travel;
        float   angle = 0.0f;
        ActorId target = kNoActor;
        Vector3 spot;

        static Facing ToAngle(float radians)
        {
            Facing f;
            f.mode = Mode::Angle;
            f.angle = radians;
            return f;
        }

        static Facing ToTarget(ActorId unit)
        {
            Facing f;
            f.mode = Mode::Target;
            f.target = unit;
            return f;
        }

        static Facing ToSpot(Vector3 const& p)
        {
            Facing f;
            f.mode = Mode::Spot;
            f.spot = p;
            return f;
        }
    };

    /**
     * @brief A running leg reduced to the scalars a SCHEDULER needs -- no geometry.
     *
     * The repair scheduler asks four questions: how long is this leg, how far into it
     * are we, how fast is it going, and is it the smooth kind. None of them need the
     * points. Taking a Leg rather than a Course is what lets the scheduler run over the
     * spline the server is executing TODAY as readily as over a Course, which is the
     * whole difference between a design that works and one that waits for the rest of
     * the rewrite to land.
     */
    struct Leg
    {
        uint32  id = 0;
        Instant startedAt = 0;
        uint32  duration = 0;
        uint32  elapsed = 0;
        float   speed = 0.0f;
        bool    smooth = false;   ///< Catmull-Rom, and therefore flying.

        bool Valid() const { return duration > 0; }

        float Progress() const
        {
            return duration ? float(elapsed) / float(duration) : 0.0f;
        }

        /**
         * @brief A leg that is already under way, described from the outside.
         *
         * `elapsed` is what the executing spline reports, so the start is derived from
         * it rather than remembered -- the caller does not have to have been present
         * when the leg was laid.
         */
        static Leg Running(uint32 id, uint32 duration, uint32 elapsed, float speed,
                           bool smooth, Instant now)
        {
            Leg leg;
            leg.id = id;
            leg.duration = duration;
            leg.elapsed = (elapsed > duration) ? duration : elapsed;
            leg.speed = speed;
            leg.smooth = smooth;
            leg.startedAt = Advance(now, -Millis(leg.elapsed));
            return leg;
        }
    };

    /// Why a course stopped being the plan.
    enum class Ending : uint8
    {
        Running,      ///< Still the plan.
        Arrived,      ///< Ran to its end.
        Superseded,   ///< A newer course replaced it. The COMMON case: the retail
                      ///< captures show 53% to 85% of legs replaced before they
                      ///< finished, at a median of 35% to 80% of the way along.
        Revoked       ///< Rooted, stunned, killed, teleported.
    };

    /**
     * @brief One unit's plan over one interval of time.
     *
     * A course answers `At(t)` for any instant, which is the whole point: a unit does
     * not HAVE a position that something must remember to advance, it has a plan, and
     * where it is follows from the plan and the clock. The retail server behaves this
     * way and the captures show it -- across 7,658 consecutive leg pairs, the origin of
     * every replacement leg lay on the geometry of the one it replaced, never further
     * than 0.67 yards off (and that residual is the wire's own 0.25-yard quantisation,
     * not drift). There is no second position to diverge, because there is no second
     * position.
     *
     * Construct one with Plan(). The timing it computes is not a design choice; it is
     * the client's, recovered from the captures and asserted in CourseTest.
     */
    class Course
    {
        public:
            /// A course that is not a plan. Ended() is true for it at every instant.
            Course() = default;

            /**
             * @brief Lay a course through `points`, starting at `at`.
             *
             * @param domain  Map, or the deck the points are expressed on.
             * @param points  At least two; points[0] is where the unit is NOW.
             * @param gait    Pace. Selects nothing about the geometry.
             * @param speed   Yards per second; must be positive.
             * @param facing  Orientation to hold once the course ends.
             * @param curve   Segmented, or Smooth (which forces Gait::Fly).
             * @param at      The instant the course begins on the SERVER's clock.
             * @param id      The course id echoed on the wire.
             *
             * Returns an empty course when the arguments cannot describe a movement
             * (fewer than two points, non-positive speed). An empty course is safe to
             * evaluate: it has ended, everywhere.
             */
            static Course Plan(Domain const& domain, std::vector<Vector3> points,
                               Gait gait, float speed, Facing const& facing,
                               Curve curve, Instant at, uint32 id);

            /**
             * @brief Lay a FALL from `from` down to `to`.
             *
             * A separate constructor because a fall is a separate kind of event, not a
             * travel with an unusual speed. Its duration comes from gravity (`Helm::Fall`
             * -- the client's own constants), its shape is the vertical line between the
             * two, and the wire carries FLAG_FALLING so the client computes the elevation
             * itself instead of interpolating the chord.
             *
             * Until this existed the falling case had to be sent by the old spline
             * builder, because a Course had no way to say "this is a fall" and no way to
             * time one. That one exception is what kept the whole packet layer alive.
             *
             * @param to  The landing point. Only its Z is used for the drop; a fall goes
             *            straight down, which is what the client draws.
             */
            static Course Falling(Domain const& domain, Vector3 const& from,
                                  Vector3 const& to, Facing const& facing, Instant at,
                                  uint32 id);

            bool Empty() const { return m_points.size() < 2; }

            /// Gravity times this course, not a speed. See `Falling`.
            bool IsFalling() const { return m_falling; }

            Domain const& GetDomain() const { return m_domain; }
            std::vector<Vector3> const& Points() const { return m_points; }
            Gait GetGait() const { return m_gait; }
            Curve GetCurve() const { return m_curve; }
            Facing const& GetFacing() const { return m_facing; }
            float Speed() const { return m_speed; }
            uint32 Id() const { return m_id; }
            Instant StartedAt() const { return m_at; }

            /**
             * @brief How long the course takes, in milliseconds.
             *
             * THE number that goes on the wire, and the only thing that ties the
             * geometry to time: the client is told the total and reparameterises its
             * spline to last exactly that long. Which is why the endpoints of a course
             * agree between server and client by construction, whatever either does in
             * between.
             */
            uint32 Duration() const { return m_marks.empty() ? 0 : m_marks.back(); }

            /**
             * @brief Which point of the course the mover has most recently passed.
             *
             * Zero before the first segment ends, `Points().size() - 1` once the leg is
             * over. What waypoint movement counts arrivals with, and the reason it is a
             * question for the PLAN rather than for whatever is drawing the movement:
             * the marks are the same milliseconds the client was told, so the server and
             * the client agree about which node has been reached without either asking
             * the other.
             */
            std::size_t PointIndex(Instant now) const
            {
                if (m_marks.size() < 2)
                {
                    return 0;
                }

                const uint32 elapsed = Elapsed(now);
                std::size_t at = 0;
                while (at + 1 < m_marks.size() && m_marks[at + 1] <= elapsed)
                {
                    ++at;
                }
                return at;
            }

            /// Milliseconds elapsed, clamped to [0, Duration()].
            uint32 Elapsed(Instant now) const;

            /// This course as the scheduler sees it: scalars, no geometry.
            Leg LegAt(Instant now) const
            {
                Leg leg;
                leg.id = m_id;
                leg.startedAt = m_at;
                leg.duration = Duration();
                leg.elapsed = Elapsed(now);
                leg.speed = m_speed;
                leg.smooth = (m_curve == Curve::Smooth) || (m_gait == Gait::Fly);
                return leg;
            }

            /// Fraction travelled, in [0, 1]. What the sync packet carries.
            float Progress(Instant now) const;

            /// True once the course has run out. Not a distance test -- a clock test.
            bool Ended(Instant now) const { return Elapsed(now) >= Duration(); }

            /**
             * @brief Where the unit is at `now`.
             *
             * Exact at both ends by construction. In between it is the best estimate
             * the server can make of what the client is drawing; ask Slack() for how
             * far that estimate may be off.
             */
            Vector3 At(Instant now) const;

            /// The direction of travel at `now`, in radians, normalised to [0, 2*PI).
            /// After the course ends this is the resolved final facing.
            float Heading(Instant now) const;

            /**
             * @brief How far `At(now)` may be from what the client is actually drawing,
             *        in yards -- WITHOUT the client-clock term.
             *
             * Two contributions, both measured rather than guessed:
             *
             *  - QUANTISATION. Interior points are put on the wire as 0.25-yard
             *    offsets, so the polyline the client walks is not quite the one that
             *    was planned. Reconstructing retail's own legs from the wire and
             *    comparing the implied speed against the walk speed puts the excess
             *    path length at about 1.2%, which is the zig-zag the rounding adds.
             *  - PARAMETERISATION. Within one segment of a Smooth course the client's
             *    parameter is not arc length, so the position may lead or lag by a
             *    fraction of a segment. Segmented courses have no such term: a straight
             *    segment travelled at constant speed is the same curve either way.
             *
             * Both vanish at the ends, where the geometry is exact. Add
             * ClientClock::Uncertainty() * Speed() / 1000 for the clock term; that one
             * belongs to the session, not to the course.
             */
            float Slack(Instant now) const;

            /**
             * @brief The next instant at which this course needs attention.
             *
             * Its end, and nothing else. A course that is running needs no per-tick
             * poll -- that is what makes standing still free.
             */
            Instant EndsAt() const { return Advance(m_at, Millis(Duration())); }

            /// The point the course finishes on.
            Vector3 Destination() const
            {
                return m_points.empty() ? Vector3() : m_points.back();
            }

            /// Cumulative millisecond marks, one per point; marks[0] is 0. Exposed for
            /// the wire writer and the tests, which are the only things that need to
            /// see the timing decomposition rather than ask At().
            std::vector<uint32> const& Marks() const { return m_marks; }

        private:
            /// Index of the segment containing `elapsed`, and the fraction along it.
            void Locate(uint32 elapsed, size_t& segment, float& u) const;

            /// The four control points of segment `k`, with the ends extrapolated the
            /// way the client extrapolates them.
            void Controls(size_t k, Vector3 out[4]) const;

            Domain               m_domain;
            std::vector<Vector3> m_points;
            std::vector<uint32>  m_marks;
            Facing               m_facing;
            float                m_speed = 0.0f;
            Instant              m_at = 0;
            uint32               m_id = 0;
            Gait                 m_gait = Gait::Run;
            Curve                m_curve = Curve::Segmented;
            bool                 m_quantised = false; ///< Has interior points.
            bool                 m_falling = false;   ///< Timed by gravity, not speed.

            /// Last non-zero XY tangent. A vertical fall (and any zero-length
            /// segment) has no heading of its own; inventing +X is what spun
            /// the pose east for the whole drop.
            mutable float        m_lastHeading = 0.0f;
    };
}

#endif // MANGOS_COURSE_H
