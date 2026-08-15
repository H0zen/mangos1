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

#include "MoveSplineInit.h"
#include "Unit.h"
#include "Transports.h"
#include "TransportMap.h"
#include "Map.h"
#include "CourseWire.h"
#include "Log.h"

#include <atomic>

namespace
{
    /// The vessel whose deck this unit is standing on, or an empty guid. Derived from the
    /// map, so a leg goes out as SMSG_MONSTER_MOVE_TRANSPORT for anything on a deck --
    /// crew, pet or totem alike -- without anyone having registered it as anything.
    ObjectGuid DeckVesselGuidOf(Unit const& unit)
    {
        if (Map* on = unit.GetMap())
        {
            if (TransportMap* hull = on->AsTransport())
            {
                if (Transport* vessel = hull->Vessel())
                {
                    return vessel->GetObjectGuid();
                }
            }
        }
        return ObjectGuid();
    }

    /**
     * @brief The next leg identity.
     *
     * It used to be initialised to zero and assigned by nothing, anywhere, so every leg
     * this server ever sent carried the id 0 -- confirmed on a live capture: 31
     * monster-moves, all `id=0`. Retail's is a counter shared by the whole world, visibly
     * climbing across unrelated creatures in the sniffs.
     *
     * It is not decoration. The repair scheduler recognises a REPLACEMENT leg by its id,
     * and a replaced leg is the common case; with a constant zero it never noticed, so it
     * kept the schedule of a leg that no longer existed.
     *
     * Atomic because maps update in parallel and each drives its own movement. The value
     * is an identity, not a count -- nothing reads it back except for equality, so
     * wrapping after four billion legs costs nothing.
     */
    uint32 NextLegId()
    {
        static std::atomic<uint32> counter(1);
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    /// The pace this leg travels at, as a property rather than a flag. Read from the
    /// flags the launcher has just settled, so it cannot disagree with the speed the
    /// duration was computed from.
    Helm::Gait GaitOf(uint32 moveFlags, bool smooth)
    {
        if (smooth || (moveFlags & MOVEFLAG_FLYING))
        {
            return Helm::Gait::Fly;
        }
        if (moveFlags & MOVEFLAG_SWIMMING)
        {
            return Helm::Gait::Swim;
        }
        return (moveFlags & MOVEFLAG_WALK_MODE) ? Helm::Gait::Walk : Helm::Gait::Run;
    }
}

namespace Movement
{
    /**
     * @brief Selects the appropriate speed type based on movement flags.
     * @param moveFlags The movement flags.
     * @return The selected UnitMoveType.
     */
    UnitMoveType SelectSpeedType(uint32 moveFlags)
    {
        if (moveFlags & MOVEFLAG_FLYING)
        {
            if (moveFlags & MOVEFLAG_BACKWARD)
            {
                return MOVE_FLIGHT_BACK;
            }
            return MOVE_FLIGHT;
        }

        if (moveFlags & MOVEFLAG_SWIMMING)
        {
            if (moveFlags & MOVEFLAG_BACKWARD)
            {
                return MOVE_SWIM_BACK;
            }
            return MOVE_SWIM;
        }

        if (moveFlags & MOVEFLAG_WALK_MODE)
        {
            return MOVE_WALK;
        }

        if (moveFlags & MOVEFLAG_BACKWARD)
        {
            return MOVE_RUN_BACK;
        }

        return MOVE_RUN;
    }

    MoveSplineInit::MoveSplineInit(Unit& m) : unit(m)
    {
        // Mix the unit's existing state into the new leg.
        m_walking = unit.m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE);

        // NOT from CAN_FLY or LEVITATING. Those say the unit COULD fly, not that this leg
        // does, and `flying` here selects Catmull-Rom interpolation and the flight
        // animation -- so a levitating walker on three or more points was animated and
        // timed as a flight while travelling at walk speed. Only actually flying counts;
        // a caller that means a curve says so with SetFly().
        m_flying = unit.m_movementInfo.HasMovementFlag(MOVEFLAG_FLYING);
    }

    void MoveSplineInit::SetFacing(const Unit* target)
    {
        // Several callers hand this whatever a guid lookup returned. Dereferencing it
        // unchecked put a crash one failed lookup away from every chase that ends facing
        // its victim.
        if (!target)
        {
            return;
        }
        m_facing = Helm::Facing::ToTarget(target->GetObjectGuid().GetRawValue());
    }

    void MoveSplineInit::SetFacing(float angle)
    {
        m_facing = Helm::Facing::ToAngle(angle);
    }

    int32 MoveSplineInit::Launch()
    {
        // A DECK IS NOT A SEAT. The unit's map is the vessel and its position is already
        // deck-local, so Where() is the answer and nothing is composed or looked up.
        const ObjectGuid vesselGuid = DeckVesselGuidOf(unit);

        Helm::Domain domain;
        domain.map = unit.GetMapId();
        domain.vessel =
            vesselGuid.IsEmpty() ? Helm::kNoActor : vesselGuid.GetRawValue();

        // WHERE THE UNIT ACTUALLY IS. Mid-leg the pose is refreshed from the running
        // course every tick, so Where() is already the answer -- there is no second
        // clock left to ask, and that is the point of the engine being gone.
        const Vector3 here(unit.Where().X(), unit.Where().Y(), unit.Where().Z());

        if (m_path.empty())
        {
            MoveTo(here);
        }

        m_path[0] = here;

        uint32 moveFlags = unit.m_movementInfo.GetMovementFlags();
        if (m_walking)
        {
            moveFlags |= MOVEFLAG_WALK_MODE;
        }
        else
        {
            moveFlags &= ~MOVEFLAG_WALK_MODE;
        }
        moveFlags |= (MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD);

        if (m_velocity <= 0.0f)
        {
            m_velocity = unit.GetSpeed(SelectSpeedType(moveFlags));
        }

        const uint32 legId = NextLegId();

        // A FALL IS NOT A TRAVEL. Timed by gravity, sent with FLAG_FALLING, and the
        // client integrates the height itself from the same constants the server uses.
        if (m_falling)
        {
            const Helm::Course fall = Helm::Course::Falling(
                domain, m_path.front(), m_path.back(), m_facing, getMSTime(), legId);

            if (fall.Empty())
            {
                return 0;   // level, or upward: not a fall at all
            }

            unit.m_movementInfo.SetMovementFlags(MovementFlags(moveFlags));

            WorldPacket data;
            Helm::Wire::WriteLaunch(fall, unit.GetObjectGuid().GetRawValue(), data);
            unit.SendMessageToSet(&data, true);

            unit.SetCourse(fall);
            return int32(fall.Duration());
        }

        const bool smooth = m_flying;

        Helm::Course course = Helm::Course::Plan(
            domain, m_path, GaitOf(moveFlags, smooth), m_velocity, m_facing,
            smooth ? Helm::Curve::Smooth : Helm::Curve::Segmented, getMSTime(), legId);

        // A leg with nothing to travel -- every point at one coordinate, or a speed that
        // is not a speed. The captures show retail sending thousands of the first, so it
        // is an idiom rather than an error, but a plan to go nowhere is not a plan: say
        // nothing was launched and let the caller act on its own tick.
        if (course.Empty())
        {
            return 0;
        }

        // === CUT THE LEG SHORT; DO NOT CHANGE ITS SHAPE.
        //
        // The packed form spends eleven signed bits on X and Y and only ten on Z, in
        // quarter-yard units, so an interior point more than 256 (or 128 vertical) yards
        // from the middle of the leg wraps and the client walks somewhere else entirely.
        //
        // What used to happen was a collapse to the two endpoints, and that is the one
        // answer that cannot be right: the interior points are the corners the router
        // bent around, so the chord between the ends runs through whatever they were
        // bending around. A leg is allowed to be SHORTER than what was asked for; it is
        // not allowed to be a different path. So take the longest prefix that survives
        // packing and let the generator plan the rest on arrival, which every generator
        // here already does.
        //
        // The envelope is measured from the MIDDLE of the leg, so dropping the tail moves
        // the middle too and a prefix can fit where the whole did not -- which is why
        // this is a search rather than an arithmetic.
        if (!Helm::Wire::Fits(course))
        {
            size_t keep = m_path.size() - 1;
            Helm::Course cut;

            while (keep >= 2)
            {
                PointsArray prefix(m_path.begin(), m_path.begin() + keep);
                cut = Helm::Course::Plan(
                    domain, prefix, GaitOf(moveFlags, smooth), m_velocity, m_facing,
                    smooth ? Helm::Curve::Smooth : Helm::Curve::Segmented, getMSTime(),
                    legId);

                if (!cut.Empty() && Helm::Wire::Fits(cut))
                {
                    break;
                }
                --keep;
            }

            if (keep < 2 || cut.Empty())
            {
                sLog.outError("%s: leg cannot be packed even as two points",
                              unit.GetGuidStr().c_str());
                return 0;
            }

            sLog.outError("%s: path will not survive packing; walking %zu of %zu points",
                          unit.GetGuidStr().c_str(), keep, m_path.size());
            course = cut;
        }

        unit.m_movementInfo.SetMovementFlags(MovementFlags(moveFlags));

        WorldPacket data;
        Helm::Wire::WriteLaunch(course, unit.GetObjectGuid().GetRawValue(), data);
        unit.SendMessageToSet(&data, true);

        unit.SetCourse(course);
        return int32(course.Duration());
    }

    void MoveSplineInit::Stop(bool forceSend /*= false*/)
    {
        // Nothing running and nobody insisting: there is nothing to say. The insistence
        // matters -- Unit::InterruptMoving abandons the course itself and then asks for
        // the stop packet, and without the flag that request died right here. The client
        // was never told, so it flew the rest of the old leg on its own: the corpse that
        // keeps sliding, the runner that coasts past where it really stopped, and the
        // snap back when the next packet finally names the true position.
        if (!unit.IsTravelling() && !forceSend)
        {
            return;
        }

        const ObjectGuid vesselGuid = DeckVesselGuidOf(unit);

        Helm::Domain domain;
        domain.map = unit.GetMapId();
        domain.vessel =
            vesselGuid.IsEmpty() ? Helm::kNoActor : vesselGuid.GetRawValue();

        const Geometry::Vector3 here(unit.Where().X(), unit.Where().Y(),
                                     unit.Where().Z());

        unit.m_movementInfo.RemoveMovementFlag(
            MovementFlags(MOVEFLAG_FORWARD | MOVEFLAG_SPLINE_ENABLED));

        // The plan stops claiming the unit is elsewhere. Everything that asks whether a
        // leg is running reads the course, so this IS the stop as far as the server is
        // concerned; the packet below is how the client finds out.
        unit.AbandonCourse();

        WorldPacket data;
        Helm::Wire::WriteHalt(domain, here, NextLegId(),
                              unit.GetObjectGuid().GetRawValue(), data);
        unit.SendMessageToSet(&data, true);
    }
}
