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

#include "CourseSync.h"

namespace Helm
{
    bool CourseSync::Eligible(Leg const& leg, Reach reach)
    {
        if (!leg.Valid())
        {
            return false;
        }

        // Too short to be worth repairing: it would end before the first repair fell
        // due. This single test is what keeps the mechanism off the two thirds of
        // retail legs that are one straight hop over in about a second.
        if (leg.duration <= uint32(kCadence + kTailGuard))
        {
            return false;
        }

        return (reach == Reach::AnyCourse) || leg.smooth;
    }

    bool CourseSync::Due(State& state, Leg const& leg, ClientClock const& clock,
                         Instant now, Reach reach)
    {
        if (!Eligible(leg, reach))
        {
            state.armed = false;
            return false;
        }

        // A different leg under the same mover: everything the state remembered is
        // about a plan that no longer exists. Given that retail replaces a leg before
        // it finishes 53% to 85% of the time, this is the common path, not the corner.
        if (!state.armed || state.courseId != leg.id)
        {
            state.armed = true;
            state.courseId = leg.id;
            state.nextAt = Advance(leg.startedAt, kCadence);
        }

        // Finished, or close enough that a correction is noise.
        if (leg.elapsed >= leg.duration ||
            (leg.duration - leg.elapsed) <= uint32(kTailGuard))
        {
            return false;
        }

        if (Since(now, state.nextAt) < 0)
        {
            // Not yet due on the cadence -- but the client may have told us it has
            // fallen far enough behind to be worth an early word.
            const Millis skew = clock.SkewSince(leg.startedAt);
            const float behind = leg.speed * float(skew) / 1000.0f;
            if (behind < kSkewYards)
            {
                return false;
            }

            // Even then, respect the floor: a burst of skip reports must not become a
            // burst of packets.
            const Millis sinceLast = Since(now, Advance(state.nextAt, -kCadence));
            if (sinceLast < kFloor)
            {
                return false;
            }
        }

        state.nextAt = Advance(now, kCadence);
        return true;
    }
}
