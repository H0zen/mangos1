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

// THE MOVEMENT LAYER, WITH NO WORLD UNDER IT.
//
// Every case here builds a Path, a Pace, a Curve or a Motion out of plain numbers and
// asserts on plain numbers. No Unit, no Map, no database, no clock the process happens
// to be running on. That is not a convenience -- it is the property the layer was
// designed for, and it is why the timing rule below can be checked against the retail
// captures at all. The inherited spline could not be evaluated without knowing which
// packet had produced it, so in fifteen years its arithmetic was never once asserted.
//
// The headline case is Pace: the client's duration accumulator, seeded at one and
// truncated per segment. It is the single most consequential number in the subsystem --
// get it wrong and the server disagrees with the client about where every mover is, for
// the whole of every leg -- and it is four lines of arithmetic that nothing prevented
// anyone from testing.

#include "TestHarness.h"

#include "motion/Curve.h"
#include "motion/Motion.h"
#include "motion/Pace.h"
#include "motion/Path.h"

#include <cmath>
#include <vector>

namespace
{
    using Geometry::Vector3;

    Helm::Path Line(float length, uint32_t map = 0)
    {
        Helm::Path path;
        std::vector<Vector3> points;
        points.push_back(Vector3(0.0f, 0.0f, 0.0f));
        points.push_back(Vector3(length, 0.0f, 0.0f));
        path.Build(points, Helm::Frame{map});
        return path;
    }

    /// `count` equal segments of `each` yards, in a straight line.
    Helm::Path Chain(size_t count, float each)
    {
        Helm::Path path;
        std::vector<Vector3> points;
        for (size_t i = 0; i <= count; ++i)
        {
            points.push_back(Vector3(float(i) * each, 0.0f, 0.0f));
        }
        path.Build(points, Helm::Frame{0});
        return path;
    }
}

// ---------------------------------------------------------------- Path

// Length is the polyline in three dimensions, not the chord and not the plan. Both
// mistakes were measured against retail and both are large: the chord implies 2.435 yd/s
// where 2.5 was sent, and dropping Z costs 28 ms at the 95th percentile on a coastal
// route.
TEST(Motion_PathLengthIsThePolylineInThreeDimensions)
{
    Helm::Path path;
    std::vector<Vector3> points;
    points.push_back(Vector3(0.0f, 0.0f, 0.0f));
    points.push_back(Vector3(3.0f, 4.0f, 0.0f));    // 5
    points.push_back(Vector3(3.0f, 4.0f, 12.0f));   // 12
    REQUIRE(path.Build(points, Helm::Frame{0}));

    CHECK(std::fabs(path.Length() - 17.0f) < 0.001f);
    CHECK(std::fabs(path.SegmentLength(0) - 5.0f) < 0.001f);
    CHECK(std::fabs(path.SegmentLength(1) - 12.0f) < 0.001f);

    // The chord is shorter, and is never what times a leg.
    CHECK(path.Chord() < path.Length());
    CHECK(std::fabs(path.Chord() - 13.0f) < 0.001f);
}

// A path refuses what it cannot describe, at the door. A NaN admitted here reappears as
// a distance, a duration, a packed offset and a position, and by then nothing can say
// where it came from.
TEST(Motion_PathRefusesTheUndescribable)
{
    Helm::Path path;

    std::vector<Vector3> single;
    single.push_back(Vector3(1.0f, 1.0f, 1.0f));
    CHECK(!path.Build(single, Helm::Frame{0}));
    CHECK(!path.Valid());

    std::vector<Vector3> nan;
    nan.push_back(Vector3(0.0f, 0.0f, 0.0f));
    nan.push_back(Vector3(std::nanf(""), 0.0f, 0.0f));
    CHECK(!path.Build(nan, Helm::Frame{0}));
    CHECK(!path.Valid());
}

// The frame travels with the geometry. Two paths in different frames are not nearly the
// same place; they are not comparable at all, and the type is what makes saying so cheap.
TEST(Motion_PathCarriesItsFrame)
{
    const Helm::Path world = Line(10.0f, 0);
    const Helm::Path deck = Line(10.0f, 5001);

    CHECK(world.InFrame() != deck.InFrame());
    CHECK(world.InFrame() == Helm::Frame{0});
}

// ---------------------------------------------------------------- Pace

// THE ACCUMULATOR. One segment: the duration is floor(1 + length * 1000 / speed), so the
// residual against the naive product lands in (0, 1] -- which is exactly the
// distribution measured over 6,961 retail single-hop legs.
TEST(Motion_PaceIsSeededAtOneAndTruncated)
{
    const float speed = 2.5f;

    for (int tenths = 1; tenths <= 400; ++tenths)
    {
        const float length = float(tenths) * 0.1f;
        const Helm::Path path = Line(length);

        Helm::Pace pace;
        REQUIRE(pace.Time(path, speed));

        const double naive = double(length) * 1000.0 / double(speed);
        const double residual = double(pace.Duration()) - naive;

        CHECK(residual > 0.0);
        CHECK(residual <= 1.0);
    }
}

// And the first mark is one, not zero. It is on the wire; removing it makes every leg a
// millisecond short and every arrival a millisecond early.
TEST(Motion_PaceStartsAtOneMillisecond)
{
    Helm::Pace pace;
    REQUIRE(pace.Time(Line(10.0f), 2.5f));
    REQUIRE(!pace.Marks().empty());
    CHECK_EQ(pace.Marks().front(), uint32_t(1));
}

// Truncation compounds per segment, so a many-point leg runs SHORT of the naive product
// by up to one millisecond a point. A 93-point leg -- retail sends one -- is up to 92 ms
// short, and a server that "corrects" this disagrees with the client all the way along.
TEST(Motion_PaceLosesAMillisecondAPointAndNotMore)
{
    const float speed = 7.0f;
    const size_t segments = 92;
    const float each = 1.37f;      // deliberately not a whole number of milliseconds

    const Helm::Path path = Chain(segments, each);

    Helm::Pace pace;
    REQUIRE(pace.Time(path, speed));

    const double naive = double(path.Length()) * 1000.0 / double(speed);
    const double lost = naive + 1.0 - double(pace.Duration());

    CHECK(lost >= 0.0);
    CHECK(lost <= double(segments));
    CHECK_EQ(pace.Marks().size(), segments + 1);
}

// Refused rather than invented: a leg with no speed has no timing, and a zero duration
// is a real idiom that comes from a path with no length instead.
TEST(Motion_PaceRefusesASpeedOfZero)
{
    Helm::Pace pace;
    CHECK(!pace.Time(Line(10.0f), 0.0f));
    CHECK(!pace.Time(Line(10.0f), -1.0f));
    CHECK(!pace.Valid());
    CHECK_EQ(pace.Duration(), uint32_t(0));
}

// The free function and the class must never drift: they are the same accumulator, and
// the whole point of having both is that one is asked without building the other.
TEST(Motion_ClientDurationAgreesWithPace)
{
    const Helm::Path path = Chain(7, 3.3f);

    Helm::Pace pace;
    REQUIRE(pace.Time(path, 8.0f));
    CHECK_EQ(Helm::ClientDuration(path, 8.0f), pace.Duration());
}

// ---------------------------------------------------------------- Curve

// A straight leg is straight. Stated because it is the case every ground creature uses,
// and because a Catmull-Rom that quietly applied to it would bend a walk into a flight.
TEST(Motion_LinearIsStraight)
{
    const Helm::Path path = Chain(2, 10.0f);
    const Vector3 middle = Helm::PointOn(path, Helm::Curve::Segmented, 0, 0.5f);

    CHECK(std::fabs(middle.x - 5.0f) < 0.001f);
    CHECK(std::fabs(middle.y) < 0.001f);
}

// Catmull-Rom passes through the points it was given, at both ends of every segment.
// That is what the clamped-end control points buy, and without them a leg begins
// slightly off the one position both sides already agree about.
TEST(Motion_CatmullRomPassesThroughItsPoints)
{
    Helm::Path path;
    std::vector<Vector3> points;
    points.push_back(Vector3(0.0f, 0.0f, 0.0f));
    points.push_back(Vector3(10.0f, 5.0f, 0.0f));
    points.push_back(Vector3(20.0f, -5.0f, 0.0f));
    points.push_back(Vector3(30.0f, 0.0f, 0.0f));
    REQUIRE(path.Build(points, Helm::Frame{0}));

    for (size_t segment = 0; segment < path.SegmentCount(); ++segment)
    {
        const Vector3 begin = Helm::PointOn(path, Helm::Curve::Smooth, segment, 0.0f);
        const Vector3 end = Helm::PointOn(path, Helm::Curve::Smooth, segment, 1.0f);

        CHECK(std::fabs(begin.x - points[segment].x) < 0.001f);
        CHECK(std::fabs(begin.y - points[segment].y) < 0.001f);
        CHECK(std::fabs(end.x - points[segment + 1].x) < 0.001f);
        CHECK(std::fabs(end.y - points[segment + 1].y) < 0.001f);
    }

    // And it bends: the arc of a curved segment is longer than its chord, which is the
    // quantity the uncertainty of a smooth leg is bounded by.
    CHECK(Helm::SegmentArc(path, Helm::Curve::Smooth, 1) >= path.SegmentLength(1));
}

// A path that stands still has no direction, and the tangent says so rather than
// answering with an axis. A stopped mover keeps the facing it had.
TEST(Motion_AStandingStillPathHasNoTangent)
{
    Helm::Path path;
    std::vector<Vector3> points;
    points.push_back(Vector3(5.0f, 5.0f, 0.0f));
    points.push_back(Vector3(5.0f, 5.0f, 0.0f));
    REQUIRE(path.Build(points, Helm::Frame{0}));

    const Vector3 tangent = Helm::TangentOn(path, Helm::Curve::Segmented, 0, 0.5f);
    CHECK(std::fabs(tangent.x) < 0.001f);
    CHECK(std::fabs(tangent.y) < 0.001f);
    CHECK(std::fabs(tangent.z) < 0.001f);
}

// ---------------------------------------------------------------- Motion

// Both ends are exact. That is the property the whole wire format is built around: the
// duration is sent, the client reparameterises to it, and so the two sides agree at the
// start and the finish whatever either believes in between.
TEST(Motion_BothEndsOfALegAreExact)
{
    const Helm::Path path = Chain(4, 12.5f);

    Helm::Motion motion;
    REQUIRE(motion.Begin(path, 7.0f, 1000, Helm::Curve::Segmented, 42));

    const Vector3 begin = motion.At(1000);
    CHECK(std::fabs(begin.x - path.Points().front().x) < 0.001f);

    const Vector3 end = motion.At(1000 + motion.Duration());
    CHECK(std::fabs(end.x - path.Points().back().x) < 0.001f);

    CHECK(motion.Ended(1000 + motion.Duration()));
    CHECK(!motion.Ended(1000 + motion.Duration() / 2));
    CHECK_EQ(motion.Id(), uint32_t(42));
}

// Past the end it clamps rather than running on, and before the start it has not begun.
// Both are asked constantly -- a tick that lands late, a leg queried before it is sent.
TEST(Motion_ClampsOutsideItsOwnSpan)
{
    const Helm::Path path = Line(20.0f);

    Helm::Motion motion;
    REQUIRE(motion.Begin(path, 2.5f, 5000));

    const Vector3 late = motion.At(5000 + motion.Duration() + 60000);
    CHECK(std::fabs(late.x - 20.0f) < 0.001f);
    CHECK(motion.Ended(5000 + motion.Duration() + 60000));
}

// The clock wraps. Unsigned subtraction is what keeps a leg that began before the wrap
// from reporting four billion milliseconds of elapsed time and finishing instantly.
TEST(Motion_SurvivesTheClockWrapping)
{
    const Helm::Path path = Line(25.0f);

    const Helm::Instant nearTheEnd = 0xFFFFF000u;

    Helm::Motion motion;
    REQUIRE(motion.Begin(path, 2.5f, nearTheEnd));

    const uint32_t half = motion.Duration() / 2;
    const Helm::Instant wrapped = nearTheEnd + half;   // wraps for a long enough leg

    CHECK_EQ(motion.Elapsed(wrapped), half);
    CHECK(!motion.Ended(wrapped));
}

// Uncertainty is zero where the two sides agree by construction and positive where they
// do not. Nothing consults it yet; it exists so that the checks which fail at the margin
// can eventually ask how sure we are instead of assuming.
TEST(Motion_UncertaintyIsZeroAtBothEnds)
{
    const Helm::Path path = Chain(6, 8.0f);

    Helm::Motion motion;
    REQUIRE(motion.Begin(path, 7.0f, 0));

    CHECK(std::fabs(motion.Uncertainty(0).Total()) < 0.001f);
    CHECK(std::fabs(motion.Uncertainty(motion.Duration()).Total()) < 0.001f);

    const Helm::Slack middle = motion.Uncertainty(motion.Duration() / 2);
    CHECK(middle.geometry > 0.0f);
    CHECK(std::fabs(middle.clock) < 0.001f);

    // A client that admits to being a frame behind is a frame's worth of yards behind.
    const Helm::Slack behind = motion.Uncertainty(motion.Duration() / 2, 70);
    CHECK(std::fabs(behind.clock - 0.070f * 7.0f) < 0.001f);
    CHECK(behind.Total() > middle.Total());
}

// A motion answers in its path's frame and never in another. Composing a deck answer
// into the world would need the hull's true pose, which this server does not have.
TEST(Motion_AnswersInItsOwnFrame)
{
    const Helm::Path deck = Line(6.0f, 5001);

    Helm::Motion motion;
    REQUIRE(motion.Begin(deck, 2.5f, 0));
    CHECK(motion.InFrame() == Helm::Frame{5001});
    CHECK(motion.InFrame() != Helm::Frame{0});
}

// A cleared motion is not a plan, and says so at every instant rather than answering
// with the leg it used to be.
TEST(Motion_AClearedMotionHasEndedAlways)
{
    Helm::Motion motion;
    REQUIRE(motion.Begin(Line(30.0f), 2.5f, 0));
    motion.Clear();

    CHECK(!motion.Valid());
    CHECK(motion.Ended(0));
    CHECK(motion.Ended(1000000));
    CHECK_EQ(motion.Duration(), uint32_t(0));
}
