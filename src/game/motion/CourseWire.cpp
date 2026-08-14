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
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "CourseWire.h"

#include "ObjectGuid.h"
#include "Opcodes.h"
#include "WorldPacket.h"

#include <cmath>

namespace
{
    void WriteVector(WorldPacket& out, Helm::Vector3 const& v)
    {
        out << v.x << v.y << v.z;
    }

    /// The boundary where an identity becomes a guid. A Course carries the raw value
    /// so that it owes the game library nothing; this file is game code and may
    /// convert freely.
    ObjectGuid Guid(Helm::ActorId id)
    {
        return ObjectGuid(uint64(id));
    }
}

namespace Helm
{
    namespace Wire
    {
        void WriteLaunch(Course const& course, ActorId mover, WorldPacket& out)
        {
            std::vector<Vector3> const& pts = course.Points();
            if (pts.size() < 2)
            {
                return;
            }

            const bool onDeck = course.GetDomain().OnDeck();
            out.SetOpcode(onDeck ? SMSG_MONSTER_MOVE_TRANSPORT : SMSG_MONSTER_MOVE);
            out << Guid(mover).WriteAsPacked();
            if (onDeck)
            {
                out << Guid(course.GetDomain().vessel).WriteAsPacked();
            }

            WriteVector(out, pts.front());
            out << uint32(course.Id());

            Facing const& facing = course.GetFacing();
            switch (facing.mode)
            {
                case Facing::Mode::Spot:
                    out << uint8(FORM_FACE_SPOT);
                    WriteVector(out, facing.spot);
                    break;
                case Facing::Mode::Target:
                    out << uint8(FORM_FACE_TARGET);
                    out << uint64(facing.target);
                    break;
                case Facing::Mode::Angle:
                    out << uint8(FORM_FACE_ANGLE);
                    out << float(facing.angle);
                    break;
                case Facing::Mode::Travel:
                default:
                    out << uint8(FORM_TRAVEL);
                    break;
            }

            out << uint32(FlagsOf(course));
            out << uint32(course.Duration());

            // The count field is the INDEX of the final point, not how many there are:
            // the origin travelled in the header and is not counted. So a straight hop
            // sends 1, and the interior points are the (index - 1) that follow.
            const uint32 lastIndex = uint32(pts.size() - 1);
            out << lastIndex;

            if (course.GetCurve() == Curve::Smooth)
            {
                // Absolute points, origin excluded.
                for (size_t i = 1; i < pts.size(); ++i)
                {
                    WriteVector(out, pts[i]);
                }
                return;
            }

            // Packed: the destination in full, then every interior point as a
            // quarter-yard offset from the midpoint of origin and destination.
            WriteVector(out, pts.back());
            if (lastIndex > 1)
            {
                const Vector3 mid = (pts.front() + pts.back()) * 0.5f;
                for (size_t i = 1; i + 1 < pts.size(); ++i)
                {
                    const Vector3 off = mid - pts[i];
                    const uint32 packed =
                        (uint32(Quantise(off.x)) & 0x7FF) |
                        ((uint32(Quantise(off.y)) & 0x7FF) << 11) |
                        ((uint32(Quantise(off.z)) & 0x3FF) << 22);
                    out << packed;
                }
            }
        }

        void WriteSync(Leg const& leg, Instant /*now*/, ActorId mover, WorldPacket& out)
        {
            out.SetOpcode(SMSG_FLIGHT_SPLINE_SYNC);
            out << float(leg.Progress());
            out << Guid(mover).WriteAsPacked();
        }

        void WriteHalt(Domain const& domain, Vector3 const& where, uint32 id,
                       ActorId mover, WorldPacket& out)
        {
            const bool onDeck = domain.OnDeck();
            out.SetOpcode(onDeck ? SMSG_MONSTER_MOVE_TRANSPORT : SMSG_MONSTER_MOVE);
            out << Guid(mover).WriteAsPacked();
            if (onDeck)
            {
                out << Guid(domain.vessel).WriteAsPacked();
            }
            WriteVector(out, where);
            out << uint32(id);
            out << uint8(FORM_HALT);
        }
    }
}
