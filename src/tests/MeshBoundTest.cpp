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

// The area-exclusion bound of Koch & Funke (SoCS 2025).
//
// The whole method rests on one property: the number returned must never exceed the
// true shortest detour through the box. Overshoot by any amount and areas that lie on
// the optimal path get discarded, the search still terminates, and the answer is
// quietly wrong -- which is exactly the failure the paper's own corner-minimum has and
// the reason this file exists.
//
// So the first test is brute force against a dense sampling of the box, and it is the
// one that matters.

#include "TestHarness.h"

#include "nav/MeshBound.hpp"

#include <cmath>
#include <limits>

namespace
{
    float Through(float sx, float sy, float tx, float ty, float px, float py)
    {
        return std::sqrt((sx - px) * (sx - px) + (sy - py) * (sy - py)) +
               std::sqrt((tx - px) * (tx - px) + (ty - py) * (ty - py));
    }

    /// The same quantity, found the stupid way.
    float Sampled(float sx, float sy, float tx, float ty,
                  float minX, float minY, float maxX, float maxY, int n)
    {
        float best = std::numeric_limits<float>::max();
        for (int i = 0; i <= n; ++i)
        {
            const float x = minX + (maxX - minX) * float(i) / float(n);
            for (int j = 0; j <= n; ++j)
            {
                const float y = minY + (maxY - minY) * float(j) / float(n);
                best = std::min(best, Through(sx, sy, tx, ty, x, y));
            }
        }
        return best;
    }
}

TEST(MeshBound_NeverExceedsTheTrueMinimum)
{
    // A box off to one side, at a spread of positions and shapes. The sampled value is
    // an upper bound on the true minimum, so the analytic answer must never be above it
    // by more than the sampling grid's own resolution.
    const float xs[] = { -40.0f, -5.0f, 0.0f, 12.0f, 60.0f };
    const float ys[] = { -30.0f, -1.0f, 3.0f, 25.0f };

    for (float bx : xs)
    {
        for (float by : ys)
        {
            for (float w : { 0.5f, 4.0f, 25.0f })
            {
                for (float h : { 0.5f, 7.0f, 18.0f })
                {
                    const float got = Nav::BoxDetourLength(
                        0.0f, 0.0f, 50.0f, 0.0f, bx, by, bx + w, by + h);
                    const float sampled = Sampled(
                        0.0f, 0.0f, 50.0f, 0.0f, bx, by, bx + w, by + h, 120);

                    // Never above: that is the safety property.
                    CHECK(got <= sampled + 1e-2f);

                    // And not slack either, or the test would pass on a constant zero.
                    CHECK(got >= sampled - 0.05f);
                }
            }
        }
    }
}

TEST(MeshBound_ABoxOnTheStraightLineRulesNothingOut)
{
    // The straight run crosses it, so the cheapest route through it is the straight run
    // itself and the box survives every upper bound that is achievable at all.
    const float d = Nav::BoxDetourLength(0.0f, 0.0f, 100.0f, 0.0f,
                                         40.0f, -5.0f, 60.0f, 5.0f);
    CHECK(std::fabs(d - 100.0f) < 1e-3f);
    CHECK(Nav::BoxCanCarryPath(0.0f, 0.0f, 100.0f, 0.0f,
                               40.0f, -5.0f, 60.0f, 5.0f, 100.0f));

    // Containing an endpoint counts as crossing.
    const float e = Nav::BoxDetourLength(0.0f, 0.0f, 100.0f, 0.0f,
                                         -2.0f, -2.0f, 2.0f, 2.0f);
    CHECK(std::fabs(e - 100.0f) < 1e-3f);
}

TEST(MeshBound_TheCornerMinimumWouldHaveBeenTooLarge)
{
    // The departure from the paper, made concrete. A box whose near edge faces the line
    // s..t: the cheapest touch is in the MIDDLE of that edge, and both its corners are
    // further. Reading the corners would report a longer detour than really exists, and
    // a longer detour is what discards an area that was allowed.
    const float sx = 0.0f, sy = 0.0f, tx = 40.0f, ty = 0.0f;
    const float minX = 10.0f, minY = 6.0f, maxX = 30.0f, maxY = 20.0f;

    const float exact = Nav::BoxDetourLength(sx, sy, tx, ty, minX, minY, maxX, maxY);

    float corners = std::numeric_limits<float>::max();
    corners = std::min(corners, Through(sx, sy, tx, ty, minX, minY));
    corners = std::min(corners, Through(sx, sy, tx, ty, maxX, minY));
    corners = std::min(corners, Through(sx, sy, tx, ty, minX, maxY));
    corners = std::min(corners, Through(sx, sy, tx, ty, maxX, maxY));

    // The mid-edge touch: mirror t across the line y = 6 and run straight to it. The
    // crossing lands at x = 20, inside the edge, so the clamp does not bite.
    const float mirrored = std::sqrt((tx - sx) * (tx - sx) + (12.0f * 12.0f));

    // sqrt(40^2 + 12^2) = 41.7612 against the corners' 42.2560. Written out rather than
    // hidden behind a margin, because the size of the gap IS the finding: half a yard on
    // a forty-yard query, and it is in the unsafe direction every time.
    CHECK(std::fabs(exact - mirrored) < 1e-2f);
    CHECK(std::fabs(exact - 41.7612f) < 1e-2f);
    CHECK(std::fabs(corners - 42.2560f) < 1e-2f);
    CHECK(corners > exact);
    CHECK(exact >= 40.0f);
}

TEST(MeshBound_ExclusionIsSymmetricInSourceAndTarget)
{
    // A detour is a detour whichever end you start from. Not decoration: the router runs
    // the same query in both directions and a bound that disagreed would make one of the
    // two searches explore an area the other had discarded.
    const float box[4] = { -30.0f, 18.0f, -10.0f, 40.0f };
    const float there = Nav::BoxDetourLength(0.0f, 0.0f, 25.0f, 5.0f,
                                             box[0], box[1], box[2], box[3]);
    const float back = Nav::BoxDetourLength(25.0f, 5.0f, 0.0f, 0.0f,
                                            box[0], box[1], box[2], box[3]);
    CHECK(std::fabs(there - back) < 1e-3f);
}

TEST(MeshBound_NoUpperBoundDiscardsNothing)
{
    // A caller with no route in hand has nothing to compare against, and must get every
    // area back. Zero and negative both mean "not known" -- the router will pass zero
    // whenever the coarse stage did not run.
    const float far[4] = { 900.0f, 900.0f, 950.0f, 950.0f };
    CHECK(Nav::BoxCanCarryPath(0.0f, 0.0f, 10.0f, 0.0f,
                               far[0], far[1], far[2], far[3], 0.0f));
    CHECK(Nav::BoxCanCarryPath(0.0f, 0.0f, 10.0f, 0.0f,
                               far[0], far[1], far[2], far[3], -1.0f));

    // With a real one, that box is plainly hopeless.
    CHECK(!Nav::BoxCanCarryPath(0.0f, 0.0f, 10.0f, 0.0f,
                                far[0], far[1], far[2], far[3], 10.5f));
}

TEST(MeshBound_AnAreaOnTheOptimalPathSurvivesItsOwnLength)
{
    // The soundness statement, as an assertion. If the upper bound IS the true optimum,
    // every area the optimum passes through must still be admitted -- otherwise the
    // method destroys the answer it was meant to find faster. Here the optimum is the
    // straight line and the boxes tile it.
    for (int i = 0; i < 10; ++i)
    {
        const float x = float(i) * 10.0f;
        CHECK(Nav::BoxCanCarryPath(0.0f, 0.0f, 100.0f, 0.0f,
                                   x, -0.5f, x + 10.0f, 0.5f, 100.0f));
    }
}
