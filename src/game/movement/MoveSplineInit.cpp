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
#include "MoveSpline.h"
#include "packet_builder.h"
#include "Unit.h"
#include "Transports.h"
#include "TransportMap.h"
#include "Map.h"
#include "CourseWire.h"
#include "Log.h"

namespace
{
    /// The vessel whose deck this unit is standing on, or an empty guid. Derived from the
    /// map, so a spline goes out as SMSG_MONSTER_MOVE_TRANSPORT for anything on a deck --
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

    /// The spline arguments' facing, in the course's vocabulary. The wire has carried
    /// this in a type byte all along, separately from the flags, so nothing is lost.
    Helm::Facing FacingOf(Movement::MoveSplineInitArgs const& args)
    {
        if (args.flags.final_angle)
        {
            return Helm::Facing::ToAngle(args.facing.angle);
        }
        if (args.flags.final_target)
        {
            return Helm::Facing::ToTarget(args.facing.target);
        }
        if (args.flags.final_point)
        {
            return Helm::Facing::ToSpot(
                Helm::Vector3(args.facing.f.x, args.facing.f.y, args.facing.f.z));
        }
        return Helm::Facing();
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
            if (moveFlags & MOVEFLAG_BACKWARD /*&& speed_obj.flight >= speed_obj.flight_back*/)
            {
                return MOVE_FLIGHT_BACK;
            }
            else
            {
                return MOVE_FLIGHT;
            }
        }
        else if (moveFlags & MOVEFLAG_SWIMMING)
        {
            if (moveFlags & MOVEFLAG_BACKWARD /*&& speed_obj.swim >= speed_obj.swim_back*/)
            {
                return MOVE_SWIM_BACK;
            }
            else
            {
                return MOVE_SWIM;
            }
        }
        else if (moveFlags & MOVEFLAG_WALK_MODE)
        {
            // if ( speed_obj.run > speed_obj.walk )
            return MOVE_WALK;
        }
        else if (moveFlags & MOVEFLAG_BACKWARD /*&& speed_obj.run >= speed_obj.run_back*/)
        {
            return MOVE_RUN_BACK;
        }

        return MOVE_RUN;
    }

    /**
     * @brief Final pass of initialization that launches spline movement.
     * @return int32 duration - estimated travel time
     */
    int32 MoveSplineInit::Launch()
    {
        MoveSpline& move_spline = *unit.movespline;

        // A DECK IS NOT A SEAT. The unit's map is the vessel and its position is already
        // deck-local, so Where() is the answer and nothing is composed or looked up.
        const ObjectGuid vesselGuid = DeckVesselGuidOf(unit);

        Location real_position(unit.Where().X(), unit.Where().Y(), unit.Where().Z(), unit.Where().Facing());

        // there is a big chance that current position is unknown if current state is not finalized, need compute it
        // this also allows calculate spline position and update map position in much greater intervals
        if (!move_spline.Finalized())
        {
            real_position = move_spline.ComputePosition();
        }

        if (args.path.empty())
        {
            // should i do the things that user should do?
            MoveTo(real_position);
        }

        // correct first vertex
        args.path[0] = real_position;
        uint32 moveFlags = unit.m_movementInfo.GetMovementFlags();
        if (args.flags.runmode)
        {
            moveFlags &= ~MOVEFLAG_WALK_MODE;
        }
        else
        {
            moveFlags |= MOVEFLAG_WALK_MODE;
        }

        moveFlags |= (MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD);

        if (args.velocity == 0.f)
        {
            args.velocity = unit.GetSpeed(SelectSpeedType(moveFlags));
        }

        if (!args.Validate(&unit))
        {
            return 0;
        }

        unit.m_movementInfo.SetMovementFlags((MovementFlags)moveFlags);
        move_spline.Initialize(args);

        // === The leg as a PLAN, and the plan is what goes on the wire. ===
        //
        // The spline above still drives the server's own idea of where the unit is and
        // when it arrives; what changes here is that the packet is no longer built by
        // walking that spline's internals. It is written from a value that computes the
        // client's timing explicitly, and three things follow immediately, on every
        // movement in the game rather than on a chosen few:
        //
        //   * the pace goes out truthfully. The old builder OR-ed the run bit into
        //     every packet it ever sent, so a creature walking at 2.5 yd/s animated as
        //     a runner. Retail leaves that bit clear on 10,540 of the legs captured.
        //   * an interior point that cannot survive the wire's quarter-yard packing is
        //     caught instead of silently wrapping (see below).
        //   * the offsets round to nearest rather than toward zero, halving an error
        //     the client cannot tell we ever had.
        Helm::Domain domain;
        domain.map = unit.GetMapId();
        domain.vessel = vesselGuid.GetRawValue();

        const bool smooth = args.flags.isSmooth();
        Helm::Course course = Helm::Course::Plan(
            domain, args.path, GaitOf(moveFlags, smooth), args.velocity,
            FacingOf(args), smooth ? Helm::Curve::Smooth : Helm::Curve::Segmented,
            getMSTime(), args.splineId);

        // A leg with nothing to travel -- every point at one coordinate. The captures
        // show retail sending thousands of them, so it is an idiom and not an error,
        // but a Course refuses to be one: a plan to go nowhere is not a plan. Until the
        // model has a form that says "stand here" honestly, that single case keeps the
        // old builder rather than being translated into something it does not mean.
        if (course.Empty())
        {
            WorldPacket legacy(SMSG_MONSTER_MOVE, 64);
            legacy << unit.GetPackGUID();
            if (!vesselGuid.IsEmpty())
            {
                legacy.SetOpcode(SMSG_MONSTER_MOVE_TRANSPORT);
                legacy << vesselGuid.WriteAsPacked();
            }
            PacketBuilder::WriteMonsterMove(move_spline, legacy);
            unit.SendMessageToSet(&legacy, true);
            return move_spline.Duration();
        }

        // The two must agree to the millisecond or the server believes an arrival the
        // client has not reached. They do by construction -- the same accumulator, the
        // same seed, the same truncation -- so a disagreement means one of them has
        // been changed without the other, and that is worth a line in the log rather
        // than a subtle drift nobody traces.
        if (uint32(move_spline.Duration()) != course.Duration())
        {
            sLog.outError("Course and spline disagree for %s: %u vs %u ms",
                          unit.GetGuidStr().c_str(), course.Duration(),
                          uint32(move_spline.Duration()));
        }

        // The packed form spends eleven signed bits on X and Y and only ten on Z, in
        // quarter-yard units, so an interior point more than 256 (or 128 vertical)
        // yards from the middle of the leg wraps and the client walks somewhere else
        // entirely. The check this restores existed but was commented out, and had the
        // limit wrong by a factor of four besides. Losing the shape of the path is a
        // poor outcome; walking a corrupted one is a worse one.
        if (!Helm::Wire::Fits(course))
        {
            sLog.outError("%s: path will not survive packing; sending its endpoints",
                          unit.GetGuidStr().c_str());
            Movement::PointsArray ends;
            ends.push_back(args.path.front());
            ends.push_back(args.path.back());
            course = Helm::Course::Plan(
                domain, ends, GaitOf(moveFlags, smooth), args.velocity,
                FacingOf(args), Helm::Curve::Segmented, getMSTime(), args.splineId);
        }

        WorldPacket data;
        Helm::Wire::WriteLaunch(course, unit.GetObjectGuid().GetRawValue(), data);
        unit.SendMessageToSet(&data, true);

        unit.SetCourse(course);
        return int32(course.Duration());
    }

    /**
     * @brief Stops any creature movement.
     */
    void MoveSplineInit::Stop(bool forceSend /*= false*/)
    {
        MoveSpline& move_spline = *unit.movespline;

        // No need to stop if we are not moving -- UNLESS the caller finalized the spline
        // itself and is relying on us to tell the client. Unit::InterruptMoving does exactly
        // that: it calls movespline->_Interrupt(), which sets splineflags.done, and then asks
        // StopMoving to force a stop. Without this flag that request died right here and the
        // client was never told, so it flew the rest of the old spline on its own -- the
        // corpse that keeps sliding, the runner that coasts past where it really stopped,
        // and the snap back when the next packet finally names the true position.
        if (move_spline.Finalized() && !forceSend)
        {
            return;
        }

        const ObjectGuid vesselGuid = DeckVesselGuidOf(unit);

        Location real_position(unit.Where().X(), unit.Where().Y(), unit.Where().Z(), unit.Where().Facing());

        // there is a big chance that current position is unknown if current state is not finalized, need compute it
        // this also allows calculate spline position and update map position in much greater intervals
        if (!move_spline.Finalized())
        {
            real_position = move_spline.ComputePosition();
        }
        if (args.path.empty())
        {
            // should i do the things that user should do?
            MoveTo(real_position);
        }

        // current first vertex
        args.path[0] = real_position;

        args.flags = MoveSplineFlag::Done;
        unit.m_movementInfo.RemoveMovementFlag(MovementFlags(MOVEFLAG_FORWARD | MOVEFLAG_SPLINE_ENABLED));
        move_spline.Initialize(args);

        WorldPacket data(SMSG_MONSTER_MOVE, 64);
        data << unit.GetPackGUID();

        if (!vesselGuid.IsEmpty())
        {
            data.SetOpcode(SMSG_MONSTER_MOVE_TRANSPORT);
            data << vesselGuid.WriteAsPacked();
        }

        data << real_position.x << real_position.y << real_position.z;
        data << move_spline.GetId();
        data << uint8(MonsterMoveStop);
        unit.SendMessageToSet(&data, true);
    }

    /**
     * @brief Constructor that initializes the MoveSplineInit with a reference to a Unit.
     * @param m Reference to the Unit to be moved.
     */
    MoveSplineInit::MoveSplineInit(Unit& m) : unit(m)
    {
        // mix existing state into new
        args.flags.runmode = !unit.m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE);
        args.flags.flying = unit.m_movementInfo.HasMovementFlag((MovementFlags)(MOVEFLAG_CAN_FLY | MOVEFLAG_FLYING | MOVEFLAG_LEVITATING));
    }

    /**
     * @brief Sets unit's facing to a specified target after all path done.
     * @param target The target to face.
     */
    void MoveSplineInit::SetFacing(const Unit* target)
    {
        args.flags.EnableFacingTarget();
        args.facing.target = target->GetObjectGuid().GetRawValue();
    }

    /**
     * @brief Adds final facing animation.
     * Sets unit's facing to specified point/angle after all path done.
     * You can have only one final facing: previous will be overridden.
     * @param angle The angle to face.
     */
    void MoveSplineInit::SetFacing(float angle)
    {
        args.facing.angle = Geometry::wrap(angle, 0.f, (float)Geometry::twoPi());
        args.flags.EnableFacingAngle();
    }
}
