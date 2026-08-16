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
    /// A ByteBuffer and not a WorldPacket: the create block is appended to an update
    /// block the caller owns, and a WorldPacket IS a ByteBuffer, so one function serves
    /// both rather than two spellings of three floats drifting apart.
    void WriteVector(ByteBuffer& out, Helm::Vector3 const& v)
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

            // NOT FORCED. This used to OR Runmode into every monster move, carried
            // over from the reference, whose own comment says only that the client has
            // strange issues without it.
            //
            // The client was read instead. Every mask tested against this word is
            // 0x400, 0x1000, 0x20000, 0x40000, 0x100000, 0x200000, 0x8000000 or
            // 0x10000000; 0x100 is not among them, in either the monster move's reader
            // or the create block's. Nothing branches on it, so forcing it only made a
            // walking creature describe itself as running.
            //
            // FlagsOf sets the bit when the gait is genuinely not a walk, which is the
            // truthful description and the only one now sent. If some behaviour turns
            // out to depend on the bit after all, this is the change to suspect.
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

        void WriteCreate(Course const& course, Instant now, ByteBuffer& out)
        {
            std::vector<Vector3> const& pts = course.Points();

            // THE FLAG WORD AND THE FACING ARE DECIDED TOGETHER, because here the flag
            // word is the only thing that tells the client how much facing to read.
            // Deciding them apart -- flags from FlagsOf, payload from facing.mode --
            // wrote a facing nothing announced, and shifted every field after it.
            Facing const& facing = course.GetFacing();
            const FacingWire wire = FacingWireOf(facing.mode);

            // The gait as it really is, plus whatever the facing costs. Runmode is
            // not forced here any more than it is in WriteLaunch -- see the note there
            // for what the client actually tests in this word, which is never 0x100.
            out << uint32(FlagsOf(course) | wire.bit);

            // wire.bytes says how many the client will now read. The rows below write
            // exactly that many, and FacingWireOf is where the two are kept equal.
            switch (facing.mode)
            {
                case Facing::Mode::Spot:
                    WriteVector(out, facing.spot);
                    break;
                case Facing::Mode::Target:
                    out << uint64(facing.target);
                    break;
                case Facing::Mode::Angle:
                    out << float(facing.angle);
                    break;
                case Facing::Mode::Travel:
                default:
                    break;   // nothing follows, and the flag word now says so
            }

            out << int32(course.Elapsed(now));
            out << int32(course.Duration());
            out << uint32(course.Id());

            // FOUR POINTS AT LEAST, AND THAT IS THE WHOLE OF THE RULE.
            //
            // The client builds its segment-length table only when the point count is
            // greater than three:
            //
            //     if ( count > 3 ) { build the length table; total length }
            //
            // Below that it does nothing at all -- and the overflow half of that table
            // is a {count, pointer} pair that NOTHING ELSE initialises. Not the
            // constructor of the parse buffer, which sets the fields around it and
            // steps over that one; not the point reader, which fills a different array.
            // So a create block of two or three points leaves a length sitting on the
            // stack, and the copy made when the unit is constructed hands it to the
            // allocator. Four bytes an entry, a stack value of -26, and the client asks
            // for 4294967192 bytes and dies. That number has been in every crash report
            // for a day.
            //
            // Retail never meets it because the reference sends the spline's internal
            // control array, phantom controls and all, so its shortest possible path
            // goes out as four. Ours went out as two.
            //
            // The padding rule, quoted rather than invented: a reflected control in
            // front (2*p0 - p1), the path, then the last point repeated. SMSG_MONSTER_
            // MOVE stays the opposite and untouched -- there the count is the INDEX of
            // the final point and no virtual controls are sent at all.
            const size_t n = pts.size();
            const Vector3 first = n ? pts.front() : Vector3();
            const Vector3 second = n > 1 ? pts[1] : first;
            const Vector3 last = n ? pts.back() : Vector3();

            // A course carries at least two points -- Course::Empty says so -- and two
            // plus the pair of controls is four. The max() is for the degenerate call
            // that should not happen and must not be a crash if it does.
            const size_t real = n < 2 ? 2 : n;
            out << uint32(real + 2);

            WriteVector(out, first + (first - second));   // the reflected control
            WriteVector(out, first);
            for (size_t i = 1; i < real; ++i)
            {
                WriteVector(out, i < n ? pts[i] : last);
            }
            WriteVector(out, last);                       // the repeated control

            WriteVector(out, last);                       // the final destination
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
