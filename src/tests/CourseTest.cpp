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
 */

/**
 * @file CourseTest.cpp
 * @brief The movement plan's arithmetic, checked against the client's.
 *
 * These are not tests of our intentions. Every constant asserted here was measured off
 * 11,518 monster-move packets in three retail captures, and the reason the assertions
 * are worth making is that being close is not good enough: a duration that is right to
 * within a few milliseconds still puts a creature somewhere the client has not drawn
 * it, and the error is invisible until a spell lands on empty ground.
 *
 * The whole file links none of the server. That is the point of a course being a value.
 */

#include "TestHarness.h"

#include "ClientClock.h"
#include "Course.h"
#include "CourseSync.h"
#include "CourseWire.h"

#include <cmath>
#include <vector>

using namespace Helm;

namespace
{
    bool Near(float a, float b, float tol)
    {
        return std::fabs(a - b) <= tol;
    }

    Domain World(uint32 map = 0)
    {
        Domain d;
        d.map = map;
        return d;
    }

    /// A straight course of `yards` at `speed`, starting at instant `at`.
    Course Hop(float yards, float speed, Gait gait = Gait::Walk, Instant at = 100000)
    {
        std::vector<Vector3> pts;
        pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
        pts.push_back(Vector3(yards, 0.0f, 0.0f));
        return Course::Plan(World(), pts, gait, speed, Facing(), Curve::Segmented,
                            at, 1);
    }

    /**
     * @brief Slack for the residual assertions below, and why they need any.
     *
     * The band being asserted is (0, 1] -- a claim about the accumulator's seed and its
     * truncation, and it is exact. What is NOT exact is the test's reconstruction of
     * "the ideal duration": it divides the nominal `yards` it asked for, while the
     * course divides the length it measured, which came back through a float sqrt. The
     * two differ in the seventh significant digit, and on a minute-long leg that is a
     * few thousandths of a millisecond -- enough to put a residual of exactly 1.0 at
     * 1.000004 and fail an assertion that is otherwise correct.
     *
     * So the tolerance is scaled to the duration and deliberately tiny. It must never
     * grow to the point where it could hide a whole millisecond, because a whole
     * millisecond is precisely what these tests exist to pin down.
     */
    double RoundOff(double idealMs)
    {
        return 1e-6 * idealMs + 1e-3;
    }
}

// ---------------------------------------------------------------------------------
// The duration formula.
// ---------------------------------------------------------------------------------

/**
 * The client is handed one number that ties geometry to time, and it reparameterises
 * its spline to last exactly that long. So this number is not ours to choose.
 *
 * On 2,057 single-hop legs across the three captures, the duration exceeded
 * length/speed*1000 by a value uniformly spread over (0, 1] ms -- medians of 0.44,
 * 0.44 and 0.53. Below the band means we arrive before the client; above it means the
 * client arrives first and stands still waiting.
 */
TEST(Course_DurationSitsInTheMeasuredResidualBand)
{
    const float speed = 2.5f;
    for (int i = 1; i <= 400; ++i)
    {
        const float yards = 0.37f * float(i);
        const Course c = Hop(yards, speed);
        REQUIRE(!c.Empty());

        const double ideal = double(yards) / double(speed) * 1000.0;
        const double residual = double(c.Duration()) - ideal;
        const double tol = RoundOff(ideal);
        CHECK(residual > -tol);
        CHECK(residual <= 1.0 + tol);
    }
}

/// The same band at the run and flight speeds the captures show.
TEST(Course_ResidualBandHoldsAtEverySpeed)
{
    const float speeds[] = { 2.5f, 7.0f, 8.0f, 10.0f, 30.0f, 45.0f };
    for (float speed : speeds)
    {
        for (int i = 1; i <= 60; ++i)
        {
            const float yards = 1.3f * float(i);
            const Course c = Hop(yards, speed, Gait::Run);
            REQUIRE(!c.Empty());
            const double ideal = double(yards) / double(speed) * 1000.0;
            const double residual = double(c.Duration()) - ideal;
            const double tol = RoundOff(ideal);
            CHECK(residual > -tol);
            CHECK(residual <= 1.0 + tol);
        }
    }
}

/**
 * The accumulator truncates after every segment, so a course loses up to a millisecond
 * per point. It is not a rounding nicety: the longest leg in the captures carries 46
 * points, which is 45 ms of drift against the naive computation -- and 45 ms at a run
 * is a third of a yard, every leg, in the same direction.
 */
TEST(Course_TruncationAccumulatesOncePerPoint)
{
    std::vector<Vector3> pts;
    const int steps = 40;
    for (int i = 0; i <= steps; ++i)
    {
        // A length that cannot land on a whole millisecond, so every segment truncates.
        pts.push_back(Vector3(float(i) * 3.33333f, 0.0f, 0.0f));
    }
    const Course c = Course::Plan(World(), pts, Gait::Walk, 2.5f, Facing(),
                                  Curve::Segmented, 5000, 7);
    REQUIRE(!c.Empty());

    const double ideal = double(steps) * 3.33333 / 2.5 * 1000.0;
    const double residual = double(c.Duration()) - ideal;

    // Seeded one ahead, then up to one lost per segment.
    CHECK(residual <= 1.0);
    CHECK(residual > -double(steps));

    // And the marks must be non-decreasing, or Locate walks off its segment.
    for (size_t i = 1; i < c.Marks().size(); ++i)
    {
        CHECK(c.Marks()[i] >= c.Marks()[i - 1]);
    }
}

// ---------------------------------------------------------------------------------
// Evaluation.
// ---------------------------------------------------------------------------------

/**
 * The ends are exact and the guarantee is structural, not statistical: the client is
 * told the total duration and the endpoints, so whatever either side does in between,
 * both agree there.
 */
TEST(Course_EndsAreExact)
{
    const Course c = Hop(50.0f, 2.5f, Gait::Walk, 900000);
    REQUIRE(!c.Empty());

    const Vector3 begin = c.At(c.StartedAt());
    CHECK(Near(begin.x, 0.0f, 0.0001f));

    const Vector3 end = c.At(Advance(c.StartedAt(), Millis(c.Duration())));
    CHECK(Near(end.x, 50.0f, 0.0001f));

    // And past the end it stays there rather than extrapolating into a wall.
    const Vector3 later = c.At(Advance(c.StartedAt(), Millis(c.Duration()) + 60000));
    CHECK(Near(later.x, 50.0f, 0.0001f));

    // Before it began, it has not moved.
    const Vector3 early = c.At(Advance(c.StartedAt(), -5000));
    CHECK(Near(early.x, 0.0f, 0.0001f));
}

/// Halfway through, halfway along -- the property that makes a straight course's
/// position a pure function of the clock.
TEST(Course_MidpointIsHalfway)
{
    const Course c = Hop(100.0f, 2.5f);
    const Vector3 mid = c.At(Advance(c.StartedAt(), Millis(c.Duration() / 2)));
    CHECK(Near(mid.x, 50.0f, 0.05f));
}

TEST(Course_ProgressRunsFromZeroToOne)
{
    const Course c = Hop(40.0f, 8.0f, Gait::Run);
    CHECK(Near(c.Progress(c.StartedAt()), 0.0f, 0.0001f));
    CHECK(Near(c.Progress(Advance(c.StartedAt(), Millis(c.Duration()))), 1.0f,
               0.0001f));
    CHECK(Near(c.Progress(Advance(c.StartedAt(), Millis(c.Duration() / 4))), 0.25f,
               0.01f));

    // The wire carries this as a float and retail never sent 1.0, so the value must
    // never leave the closed unit interval however it is asked.
    CHECK(c.Progress(Advance(c.StartedAt(), 10000000)) <= 1.0f);
    CHECK(c.Progress(Advance(c.StartedAt(), -10000)) >= 0.0f);
}

/**
 * A course is compared against a clock that wraps every 49.7 days. Comparing the raw
 * stamps would freeze every unit in the world for one instant every seven weeks --
 * which is exactly the kind of failure that is never reproduced and never fixed.
 */
TEST(Course_SurvivesTheClockWrap)
{
    const Instant justBeforeWrap = 0xFFFFF000u;
    const Course c = Hop(100.0f, 2.5f, Gait::Walk, justBeforeWrap);
    REQUIRE(!c.Empty());
    REQUIRE(c.Duration() > 0x2000u);   // The course must actually straddle the wrap.

    CHECK(!c.Ended(justBeforeWrap));
    CHECK(!c.Ended(Advance(justBeforeWrap, Millis(c.Duration() / 2))));
    CHECK(c.Ended(Advance(justBeforeWrap, Millis(c.Duration()))));

    const Vector3 mid = c.At(Advance(justBeforeWrap, Millis(c.Duration() / 2)));
    CHECK(Near(mid.x, 50.0f, 0.1f));
}

/// Slack is a promise about the ends: there, we are not guessing.
TEST(Course_SlackVanishesAtTheEnds)
{
    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(30.0f, 8.0f, 0.0f));
    pts.push_back(Vector3(60.0f, 0.0f, 0.0f));
    const Course c = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                  Curve::Segmented, 200000, 3);
    REQUIRE(!c.Empty());

    CHECK(Near(c.Slack(c.StartedAt()), 0.0f, 0.0001f));
    CHECK(Near(c.Slack(Advance(c.StartedAt(), Millis(c.Duration()))), 0.0f, 0.0001f));

    // In between there IS uncertainty, and pretending otherwise is the lie this whole
    // exercise exists to stop telling.
    CHECK(c.Slack(Advance(c.StartedAt(), Millis(c.Duration() / 2))) > 0.0f);
}

/// A degenerate course is not a course. Handing one back would make Ended() true at the
/// instant it was laid, and the caller would relay it forever.
TEST(Course_RejectsWhatCannotBeTravelled)
{
    CHECK(Hop(10.0f, 0.0f).Empty());
    CHECK(Hop(10.0f, -2.0f).Empty());

    std::vector<Vector3> one;
    one.push_back(Vector3(1.0f, 2.0f, 3.0f));
    CHECK(Course::Plan(World(), one, Gait::Run, 8.0f, Facing(), Curve::Segmented,
                       0, 1).Empty());

    std::vector<Vector3> same;
    same.push_back(Vector3(5.0f, 5.0f, 5.0f));
    same.push_back(Vector3(5.0f, 5.0f, 5.0f));
    CHECK(Course::Plan(World(), same, Gait::Run, 8.0f, Facing(), Curve::Segmented,
                       0, 1).Empty());
}

/// One bit selects both the interpolation and the flying animation, so a smooth course
/// IS a flying course. Resolving that here means no caller ever ships the contradiction.
TEST(Course_SmoothForcesFlight)
{
    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(20.0f, 10.0f, 5.0f));
    pts.push_back(Vector3(40.0f, 0.0f, 10.0f));
    const Course c = Course::Plan(World(), pts, Gait::Walk, 30.0f, Facing(),
                                  Curve::Smooth, 0, 1);
    REQUIRE(!c.Empty());
    CHECK(c.GetGait() == Gait::Fly);
    CHECK((Wire::FlagsOf(c) & Wire::FLAG_SMOOTH_FLYING) != 0);
}

/// A curve is longer than its chord and the client charges for the difference.
TEST(Course_SmoothCostsMoreThanItsChord)
{
    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(20.0f, 20.0f, 0.0f));
    pts.push_back(Vector3(40.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(60.0f, 20.0f, 0.0f));

    const Course smooth = Course::Plan(World(), pts, Gait::Fly, 30.0f, Facing(),
                                       Curve::Smooth, 0, 1);
    const Course straight = Course::Plan(World(), pts, Gait::Run, 30.0f, Facing(),
                                         Curve::Segmented, 0, 1);
    REQUIRE(!smooth.Empty());
    REQUIRE(!straight.Empty());
    CHECK(smooth.Duration() > straight.Duration());
}

// ---------------------------------------------------------------------------------
// The wire.
// ---------------------------------------------------------------------------------

/**
 * The bug this replaces, stated as a test.
 *
 * The previous packet builder OR-ed the run bit into every monster-move it ever sent,
 * with a comment saying the client had "strange issues" without it. Retail sends it
 * clear on 4,222 of the 7,097 legs in the largest capture, and those legs imply exactly
 * the walk speed. So every walking creature was animating as a runner.
 */
TEST(Wire_AWalkingCourseDoesNotClaimToRun)
{
    const Course walk = Hop(20.0f, 2.5f, Gait::Walk);
    CHECK((Wire::FlagsOf(walk) & Wire::FLAG_RUNNING) == 0);

    const Course run = Hop(20.0f, 8.0f, Gait::Run);
    CHECK((Wire::FlagsOf(run) & Wire::FLAG_RUNNING) != 0);

    // And a walk is not accidentally a flight either.
    CHECK((Wire::FlagsOf(walk) & Wire::FLAG_SMOOTH_FLYING) == 0);
}

/**
 * The packed form spends 11 signed bits on X, 11 on Y and only 10 on Z, in quarter-yard
 * units. The check this replaces used 1024 yards on all three axes -- four times the
 * real X/Y capacity and eight times Z -- and was commented out. Retail's own legs reach
 * 153.25 and 60.25 yards, so the margin is not comfortable enough to leave unguarded.
 */
TEST(Wire_PackingEnvelopeIsAsymmetricAndReal)
{
    auto detour = [](float dx, float dy, float dz)
    {
        std::vector<Vector3> pts;
        pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
        pts.push_back(Vector3(dx, dy, dz));
        pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
        return Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                            Curve::Segmented, 0, 1);
    };

    // The offset is measured from the midpoint of origin and destination, which here is
    // the origin, so the interior point's own coordinates are the offsets.
    CHECK(Wire::Fits(detour(150.0f, 150.0f, 60.0f)));    // What retail actually sends.
    CHECK(!Wire::Fits(detour(300.0f, 0.0f, 0.0f)));      // Past the X capacity.
    CHECK(!Wire::Fits(detour(0.0f, 300.0f, 0.0f)));      // Past the Y capacity.

    // The one that matters: 200 yards is comfortably inside X and Y, and outside Z.
    CHECK(Wire::Fits(detour(200.0f, 0.0f, 0.0f)));
    CHECK(!Wire::Fits(detour(0.0f, 0.0f, 200.0f)));

    // A two-point course encodes no interior point and can never overflow.
    CHECK(Wire::Fits(Hop(4000.0f, 8.0f, Gait::Run)));
}

/**
 * We emit only the bits whose position AND payload the captures settle.
 *
 * This is not caution for its own sake. The 2.4.3 flag table in this tree calls
 * 0x00000008 `Unknown4` and assumes it carries nothing; the captures say otherwise --
 * 115 packets could not be decoded at all until the bit was read as a parabolic arc
 * with eight bytes of payload after the duration, and then all 115 decoded exactly. A
 * flag that changes the LENGTH of a packet cannot be emitted on a guess: everything
 * after it in the stream would be misread.
 *
 * So the flag word a course produces must never contain a bit we have not earned.
 */
TEST(Wire_OnlyTheVerifiedBitsAreEverEmitted)
{
    const uint32 earned = Wire::FLAG_RUNNING | Wire::FLAG_SMOOTH_FLYING;

    const Gait gaits[] = { Gait::Walk, Gait::Run, Gait::Swim, Gait::Fly };
    const Curve curves[] = { Curve::Segmented, Curve::Smooth };
    for (Gait g : gaits)
    {
        for (Curve v : curves)
        {
            std::vector<Vector3> pts;
            pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
            pts.push_back(Vector3(30.0f, 0.0f, 0.0f));
            pts.push_back(Vector3(60.0f, 5.0f, 0.0f));
            const Course c = Course::Plan(World(), pts, g, 7.0f, Facing(), v, 0, 1);
            REQUIRE(!c.Empty());

            const uint32 flags = Wire::FlagsOf(c);
            CHECK((flags & ~earned) == 0);

            // In particular, never the two that would change the packet's length.
            CHECK((flags & Wire::FLAG_PARABOLIC) == 0);
            CHECK((flags & Wire::FLAG_NO_SPLINE) == 0);
        }
    }
}

/// Rounding to nearest rather than toward zero halves the error, and the client cannot
/// tell the difference -- it only multiplies by the quantum.
TEST(Wire_QuantisationRoundsToNearest)
{
    CHECK_EQ(Wire::Quantise(0.0f), 0);
    CHECK_EQ(Wire::Quantise(0.25f), 1);
    CHECK_EQ(Wire::Quantise(0.24f), 1);      // Truncation would have said 0.
    CHECK_EQ(Wire::Quantise(-0.24f), -1);
    CHECK_EQ(Wire::Quantise(1.13f), 5);      // 4.52 quarters.
    CHECK_EQ(Wire::Quantise(-1.13f), -5);
}

// ---------------------------------------------------------------------------------
// The client's clock.
// ---------------------------------------------------------------------------------

/**
 * The client volunteers how much movement time it has lost, and it is not a rare event:
 * 1,198 reports across the captures, medians of 74 to 189 ms, single reports as large
 * as 29.4 seconds. It is a debt, so it accumulates.
 */
TEST(Clock_SkewIsADebtThatAccumulates)
{
    ClientClock clock;
    clock.Sync(1000, 1000, 1060);            // 60 ms round trip.
    CHECK(clock.Known());

    clock.Skipped(70, 2000);
    clock.Skipped(70, 2100);
    clock.Skipped(250, 2400);
    CHECK_EQ(clock.TotalSkew(), 390);

    // Only what happened after the asking instant counts.
    CHECK_EQ(clock.SkewSince(1000), 390);
    CHECK_EQ(clock.SkewSince(2150), 250);
    CHECK_EQ(clock.SkewSince(9000), 0);

    // The client never claims to have run ahead of itself.
    clock.Skipped(-500, 2500);
    CHECK_EQ(clock.TotalSkew(), 390);
}

TEST(Clock_UnsyncedIsHonestlyUncertain)
{
    ClientClock clock;
    CHECK(!clock.Known());
    CHECK(clock.Uncertainty(50000) >= 1000);   // Says so loudly.

    clock.Sync(10000, 10000, 10040);           // 40 ms round trip.
    CHECK(clock.Known());
    CHECK(clock.Uncertainty(10040) < 200);
}

/// A session that stops answering is a session whose clock is drifting unobserved.
TEST(Clock_UncertaintyGrowsWhileTheClientIsSilent)
{
    ClientClock clock;
    clock.Sync(10000, 10000, 10040);
    const Millis fresh = clock.Uncertainty(10040);
    const Millis quiet = clock.Uncertainty(10040 + 60000);
    CHECK(quiet > fresh);
    CHECK(quiet >= 40000);   // Two cadences of grace, then it counts.
}

// ---------------------------------------------------------------------------------
// Repair rather than replace.
// ---------------------------------------------------------------------------------

/**
 * The scheduler takes a Leg, not a Course, and that is the point: the same code must
 * drive the spline the server executes today. A leg described from the outside -- "this
 * much of this much has gone by" -- must place its start correctly without having been
 * present when it was laid.
 */
TEST(Sync_ALegCanBeDescribedFromOutside)
{
    const Instant now = 500000;
    const Leg leg = Leg::Running(77, 30000, 12000, 8.0f, false, now);
    CHECK_EQ(leg.id, 77u);
    CHECK_EQ(leg.duration, 30000u);
    CHECK_EQ(leg.elapsed, 12000u);
    CHECK_EQ(leg.startedAt, Advance(now, -12000));
    CHECK(Near(leg.Progress(), 0.4f, 0.001f));

    // An elapsed past the end is clamped, not wrapped -- an overrun must not read as a
    // leg that has barely begun.
    const Leg over = Leg::Running(77, 30000, 99999, 8.0f, false, now);
    CHECK_EQ(over.elapsed, 30000u);
    CHECK(Near(over.Progress(), 1.0f, 0.001f));

    // And a Course produces the same view of itself.
    const Course c = Hop(80.0f, 8.0f, Gait::Run, 1000);
    const Leg from = c.LegAt(Advance(c.StartedAt(), 3000));
    CHECK_EQ(from.duration, c.Duration());
    CHECK_EQ(from.elapsed, 3000u);
    CHECK_EQ(from.startedAt, c.StartedAt());
}

/// 52% of retail's legs are one straight hop and most are over in a second or two.
/// Repair must never touch them.
TEST(Sync_ShortCoursesAreNeverRepaired)
{
    ClientClock clock;
    clock.Sync(0, 0, 40);

    const Course brief = Hop(8.0f, 8.0f, Gait::Run, 1000);   // One second.
    CHECK(!CourseSync::Eligible(brief.LegAt(1000), CourseSync::Reach::AnyCourse));

    CourseSync::State state;
    CHECK(!CourseSync::Due(state, brief.LegAt(1500), clock, 1500,
                           CourseSync::Reach::AnyCourse));
}

/// The default reproduces retail: flights, and nothing else.
TEST(Sync_FlyingOnlyIsWhatRetailDoes)
{
    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(1500.0f, 0.0f, 0.0f));

    const Course flight = Course::Plan(World(), pts, Gait::Fly, 30.0f, Facing(),
                                       Curve::Smooth, 1000, 1);
    const Course sprint = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                       Curve::Segmented, 1000, 2);
    REQUIRE(!flight.Empty());
    REQUIRE(!sprint.Empty());

    CHECK(CourseSync::Eligible(flight.LegAt(1000), CourseSync::Reach::FlyingOnly));
    CHECK(!CourseSync::Eligible(sprint.LegAt(1000), CourseSync::Reach::FlyingOnly));

    // The same long ground course IS eligible once the policy is widened -- the packet
    // names a unit and a fraction, and knows nothing about flying.
    CHECK(CourseSync::Eligible(sprint.LegAt(1000), CourseSync::Reach::AnyCourse));
}

TEST(Sync_SettlesOntoTheCadence)
{
    ClientClock clock;
    clock.Sync(0, 0, 40);

    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(800.0f, 0.0f, 0.0f));
    const Course c = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                  Curve::Segmented, 1000, 5);
    REQUIRE(!c.Empty());

    CourseSync::State state;
    auto due = [&](Instant t)
    {
        return CourseSync::Due(state, c.LegAt(t), clock, t,
                               CourseSync::Reach::AnyCourse);
    };
    CHECK(!due(2000));
    CHECK(!due(5000));
    CHECK(due(6001));
    CHECK(!due(6500));
    CHECK(due(11500));
}

/// The part a fixed cadence cannot do: the client says it has fallen behind, so we
/// answer before the timer would have.
TEST(Sync_ReportedSkewTriggersEarly)
{
    ClientClock clock;
    clock.Sync(0, 0, 40);

    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(800.0f, 0.0f, 0.0f));
    const Course c = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                  Curve::Segmented, 1000, 5);

    CourseSync::State state;
    auto due = [&](Instant t)
    {
        return CourseSync::Due(state, c.LegAt(t), clock, t,
                               CourseSync::Reach::AnyCourse);
    };
    CHECK(!due(2000));

    // Half a second of lost time at 8 yd/s is four yards -- past the threshold, and
    // past what a melee range check tolerates.
    clock.Skipped(500, 2100);
    CHECK(due(2200));

    // But a burst of reports must not become a burst of packets.
    clock.Skipped(500, 2300);
    CHECK(!due(2400));
}

/// Retail sent 484 syncs and the largest progress in any of them was 0.9993. The last
/// stretch is left alone: the arrival is exact anyway.
TEST(Sync_TheTailIsLeftAlone)
{
    ClientClock clock;
    clock.Sync(0, 0, 40);

    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(800.0f, 0.0f, 0.0f));
    const Course c = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                  Curve::Segmented, 1000, 5);
    REQUIRE(!c.Empty());

    CourseSync::State state;
    const Instant nearEnd = Advance(c.StartedAt(), Millis(c.Duration()) - 500);
    CHECK(!CourseSync::Due(state, c.LegAt(nearEnd), clock, nearEnd,
                           CourseSync::Reach::AnyCourse));

    const Instant afterEnd = Advance(c.StartedAt(), Millis(c.Duration()) + 10);
    CHECK(!CourseSync::Due(state, c.LegAt(afterEnd), clock, afterEnd,
                           CourseSync::Reach::AnyCourse));
}

/// A replacement course resets the schedule. Retail replaces a leg before it finishes
/// 53% to 85% of the time, so this is the common path, not the corner.
TEST(Sync_ANewCourseResetsTheSchedule)
{
    ClientClock clock;
    clock.Sync(0, 0, 40);

    std::vector<Vector3> pts;
    pts.push_back(Vector3(0.0f, 0.0f, 0.0f));
    pts.push_back(Vector3(800.0f, 0.0f, 0.0f));

    const Course first = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                      Curve::Segmented, 1000, 5);
    CourseSync::State state;
    CHECK(CourseSync::Due(state, first.LegAt(6001), clock, 6001,
                          CourseSync::Reach::AnyCourse));

    // A different course under the same mover: the cadence starts over from ITS start,
    // not from whatever the old one had scheduled.
    const Course second = Course::Plan(World(), pts, Gait::Run, 8.0f, Facing(),
                                       Curve::Segmented, 6100, 6);
    CHECK(!CourseSync::Due(state, second.LegAt(6200), clock, 6200,
                           CourseSync::Reach::AnyCourse));
    CHECK(CourseSync::Due(state, second.LegAt(11200), clock, 11200,
                          CourseSync::Reach::AnyCourse));
}
