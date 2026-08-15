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

#ifndef MANGOS_COURSEWIRE_H
#define MANGOS_COURSEWIRE_H

#include "Course.h"

#include <cmath>

class ByteBuffer;
class WorldPacket;

/**
 * @brief The only place a Course becomes bytes.
 *
 * The course and the packet are the same object seen twice, and keeping the translation
 * in one small file is what keeps them from drifting apart. Everything here was read
 * off 11,518 monster-move packets in three retail captures rather than inherited: the
 * field order, the meaning of every flag bit that was ever seen set, the two geometry
 * encodings and which one goes with which flag, and the real capacity of the packed
 * form.
 */
namespace Helm
{
    namespace Wire
    {
        /**
         * @brief The flag bits, and the evidence for each.
         *
         * SEVEN bits were ever seen set across four captures (32,806 legs) of a 3.0
         * client. Six sit exactly where this tree's own 2.4.3 table puts them; the
         * seventh is named `Unknown4` there and is nothing of the sort, which is the
         * reason the comparison was worth making rather than assumed.
         *
         * The bits this writer never emits are still named, because a bit that is
         * merely unnamed gets reinvented -- and one of them changes the LENGTH of the
         * packet, which makes it a decoding hazard and not just a missing feature.
         */
        enum Flag : uint32
        {
            /// Stop. Carried in the type byte instead, and stripped from the flag word
            /// before it goes out, so the captures cannot confirm this position: no
            /// packet ever contains it. Named from the 2.4.3 table; nothing here
            /// depends on it.
            FLAG_HALT = 0x00000001,

            /// The client computes the elevation itself. Five legs in the captures.
            FLAG_FALLING = 0x00000002,

            /**
             * @brief A PARABOLIC arc -- and the one flag whose payload lengthens the
             *        packet.
             *
             * NOT EMITTED, and deliberately so. This tree's 2.4.3 table calls the bit
             * `Unknown4` and treats it as payload-free, which is why 141 packets
             * across the captures could not be decoded at all until the bit was taken
             * seriously: every one carries eight extra bytes after the duration, and
             * with them all 141 decode exactly, to the byte.
             *
             * What the eight bytes are, from those 141 samples:
             *
             *  - a float that is 19.29 in 51 of them -- WoW's own fall acceleration --
             *    and otherwise a tuned value from 3.15 to 352.52. A vertical
             *    ACCELERATION, then, not a launch speed: the common case is a body
             *    left to gravity.
             *  - a uint32 that is 0 in 85 of them and, in the rest, a value between
             *    35,932 and 54,072. Too large to be an offset into legs whose median
             *    duration is 1,309 ms, so it is a stamp on some other clock, not a
             *    position within this course.
             *
             * The legs are short (median 1.3 s) and 85 of them carry a single
             * destination: a jump. Eighteen pair the bit with FLAG_NO_SPLINE -- an arc
             * with no path to follow at all.
             *
             * All of that is a 3.0 observation. Whether the 2.4.3 client reads the
             * same two fields at the same offsets is NOT answerable from these
             * captures, and emitting it on a guess would corrupt every packet after it
             * in the stream. So a course cannot ask for one; when a 2.4.3 capture
             * settles it, the arc becomes a property of Course and is written here.
             */
            FLAG_PARABOLIC = 0x00000008,

            /// Running. Clear means WALKING, and it is the client's animation
            /// selector: legs sent with it clear imply 2.5 yd/s, legs with it set
            /// imply 7 to 10.
            FLAG_RUNNING = 0x00000100,

            /// Catmull-Rom interpolation AND the flying animation, inseparably.
            /// Every leg sent as an absolute point array had it; no packed leg did.
            FLAG_SMOOTH_FLYING = 0x00000200,

            /// No path to interpolate. Eighteen legs, every one of them also
            /// parabolic. Not emitted for the same reason.
            FLAG_NO_SPLINE = 0x00000400,

            /// A closed patrol: the client loops the path instead of stopping. Seen
            /// once, on a 36.3-second leg, together with FLAG_ENTER_CYCLE -- and at
            /// the position the 2.4.3 table gives it, which is worth having confirmed
            /// even by one packet. Not emitted: a Course has one end, and a looping
            /// course is a different type with a different arrival contract.
            FLAG_CYCLIC = 0x00100000,

            /// Rides with FLAG_CYCLIC and tells the client to drop the first vertex
            /// after the first lap. Same single observation, same position, likewise
            /// not emitted.
            FLAG_ENTER_CYCLE = 0x00200000
        };

        /// The type byte, which carries the final facing entirely outside the flags.
        enum Form : uint8
        {
            FORM_TRAVEL = 0,
            FORM_HALT = 1,
            FORM_FACE_SPOT = 2,
            FORM_FACE_TARGET = 3,
            FORM_FACE_ANGLE = 4
        };

        /**
         * @brief The seat byte belongs to 3.x, NOT to 2.4.3. It is not written.
         *
         * The captures in MOTION.md show `packGUID vessel` followed by a `uint8 seat`
         * on every one of 1,737 deck legs, 0xFF on 1,733 of them -- and that is real,
         * for the client that produced them. That client is 3.0. MOTION.md says so in
         * its own second paragraph, and calls the gap the standing caveat.
         *
         * This server is 2.4.3, and three things in this tree say the byte does not
         * exist here:
         *
         *  - MovementInfo::Read, the 2.4.3 layout this core actually parses, reads a
         *    transport block of `guid, x, y, z, o, time` and stops. The seat entered
         *    that block in 3.x, alongside the second transport timestamp;
         *  - every pre-Helm writer of SMSG_MONSTER_MOVE_TRANSPORT in this tree wrote
         *    the vessel guid and went straight to the coordinates;
         *  - CLAUDE.md forbids introducing 2.5+/WotLK assumptions.
         *
         * Writing it anyway is exactly the corruption it looks like it prevents: one
         * spurious byte shifts every field after it, so the client reads the low byte
         * of X as part of the vessel guid's tail and the position lands elsewhere.
         *
         * Kept as a named constant rather than deleted, because the value is the right
         * one the day this tree is ported forward -- and because a bare 0xFF appearing
         * in a deck packet in future should have to argue with this comment first.
         */
        constexpr uint8 kSeat3xStanding = 0xFF;

        /// The quantum an interior point is rounded to before it goes on the wire.
        constexpr float kQuantum = 0.25f;

        /**
         * @brief How far an interior point may lie from the midpoint of the course.
         *
         * The packed form spends 11 signed bits on X, 11 on Y and only 10 on Z, all in
         * quarter-yard units. So the envelope is not a single number, and Z is half of
         * what the other two are -- a fact worth stating loudly, because the check this
         * replaces used 1024 yards for all three axes, which overstates X and Y by a
         * factor of four and Z by eight, and was commented out besides.
         *
         * The margin is not generous. The largest offsets retail actually sent were
         * 153.25 on X and 150.00 on Y (a coastal route) and 62.00 on Z (a dungeon, of
         * all places) -- 60% of the X/Y budget and 48% of the smaller Z one.
         */
        constexpr float kMaxOffsetXY = 256.0f;
        constexpr float kMaxOffsetZ = 128.0f;

        /**
         * @brief The envelope as the wire actually sees it: whole quanta, signed.
         *
         * These, and not the yard figures above, are what a fits-test has to compare
         * against. The yard figures are the envelope ROUNDED OUTWARDS to a whole
         * number, and the gap between the two is where the corruption lives: 11 signed
         * bits hold [-1024, 1023] quarter-yards, which is [-256, +255.75] yards, not
         * [-256, +256].
         *
         * An offset of 255.875 yards passes a "< 256 yards" test, quantises to
         * lround(1023.5) = 1024, and `1024 & 0x7FF` is 0 -- the point lands exactly on
         * the midpoint of the course. On Z it is worse: 127.875 yards quantises to 512,
         * and `512 & 0x3FF` read back as a signed 10-bit value is -512, so the point
         * does not merely collapse, it flips to the far side.
         *
         * Rare -- the largest offsets retail was seen to send are 153.25 and 62.00 --
         * but it is exactly the class of corruption Fits exists to prevent, and it was
         * getting through.
         */
        constexpr int32 kPackedMinXY = -1024;
        constexpr int32 kPackedMaxXY = 1023;
        constexpr int32 kPackedMinZ = -512;
        constexpr int32 kPackedMaxZ = 511;

        /**
         * @brief The longest point path retail was seen to send, for reference.
         *
         * Not a limit imposed here -- the packed form's only bound is the envelope
         * above, and the count field is a full uint32. It is recorded because the
         * router in this tree caps a path at 74 points (Nav::MAX_POINTS, and
         * Nav::Corridor::CAPACITY to match), and the client evidently accepts rather
         * more than that: one leg in the dungeon capture carries 93. So the 74 is a
         * budget this server chose, not a ceiling the client imposes, and a future
         * router is free to raise it without asking the wire's permission.
         */
        constexpr uint32 kLongestObserved = 93;

        /// Quantise one axis the way the wire does. Rounds to NEAREST rather than
        /// toward zero, which halves the error for free -- the client only multiplies
        /// by the quantum and cannot tell how the value was chosen.
        inline int32 Quantise(float yards)
        {
            return int32(std::lround(yards / kQuantum));
        }

        /**
         * @brief Does one interior offset survive being packed?
         *
         * Asked of the QUANTISED value, which is the value the wire carries. Testing
         * the yards instead leaves a quarter-yard-wide crack at each end of each axis
         * where the offset rounds up out of range and the mask silently folds it --
         * see kPackedMinXY.
         */
        inline bool PackedFits(Vector3 const& offset)
        {
            const int32 x = Quantise(offset.x);
            const int32 y = Quantise(offset.y);
            const int32 z = Quantise(offset.z);

            return x >= kPackedMinXY && x <= kPackedMaxXY &&
                   y >= kPackedMinXY && y <= kPackedMaxXY &&
                   z >= kPackedMinZ && z <= kPackedMaxZ;
        }

        /**
         * @brief The flag word this course puts on the wire.
         *
         * The truthfulness of the pace bit is the whole point, and it is testable
         * here without a packet. That claim is not hypothetical: the implementation
         * this replaces OR-ed the run bit into every packet unconditionally, so every
         * creature animated as running whatever its actual pace, while the duration
         * said otherwise. Retail leaves the bit clear on 4,222 of the 7,097 legs in
         * the largest capture.
         */
        inline uint32 FlagsOf(Course const& course)
        {
            uint32 flags = 0;

            // A FALL, and nothing else about the leg matters once this bit is set: the
            // client stops interpolating the geometry's height and integrates gravity
            // itself. That is why a fall may be sent as two points with no shape -- the
            // shape is the client's to compute, and it computes the same one the server
            // does because both use the same constants (Helm::Fall).
            if (course.IsFalling())
            {
                return FLAG_FALLING;
            }

            if (course.GetGait() != Gait::Walk)
            {
                flags |= FLAG_RUNNING;
            }

            // One bit for two meanings, so Course::Plan already forced the gait to
            // match; this is the other half of that bargain.
            if (course.GetCurve() == Curve::Smooth || course.GetGait() == Gait::Fly)
            {
                flags |= FLAG_SMOOTH_FLYING;
            }
            return flags;
        }

        /**
         * @brief Can this course survive the trip?
         *
         * A course whose interior points fall outside the envelope cannot be sent in
         * the packed form: the offsets wrap, and the client draws a path through
         * somewhere else entirely. Ask before writing; split the course if the answer
         * is no.
         */
        inline bool Fits(Course const& course)
        {
            // Only the packed form has an envelope. A smooth course sends absolute
            // points and is bounded by nothing but the point count.
            if (course.GetCurve() == Curve::Smooth)
            {
                return true;
            }

            std::vector<Vector3> const& pts = course.Points();
            if (pts.size() <= 2)
            {
                return true;   // No interior point is ever encoded.
            }

            const Vector3 mid = (pts.front() + pts.back()) * 0.5f;
            for (size_t i = 1; i + 1 < pts.size(); ++i)
            {
                if (!PackedFits(mid - pts[i]))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Write the course as a monster-move body.
         *
         * The packet's opcode is chosen by the caller from Course::GetDomain(): a deck
         * course is SMSG_MONSTER_MOVE_TRANSPORT and carries the vessel and a seat byte
         * first. Retail sends 0xFF for the seat on 1,331 of the 1,335 deck legs
         * captured -- standing on a deck is not sitting in a seat.
         *
         * @param course The plan.
         * @param mover  Whose plan it is.
         * @param out    Packet to fill; the opcode is set here too.
         */
        void WriteLaunch(Course const& course, ActorId mover, WorldPacket& out);

        /**
         * @brief The running leg, inside an object's create block.
         *
         * SMSG_UPDATE_OBJECT rather than SMSG_MONSTER_MOVE: what it answers is "this
         * unit you are only now seeing is part way through a movement", so it carries
         * how much of the leg has already elapsed and the WHOLE path including the point
         * it started from -- where a monster-move omits that point, having sent it in
         * the header.
         *
         * Appended to a ByteBuffer, not a WorldPacket: the caller is in the middle of
         * building an update block and owns the opcode.
         */
        void WriteCreate(Course const& course, Instant now, ByteBuffer& out);

        /**
         * @brief Write the eleven-byte progress correction.
         *
         * A float in [0, 1] and a packed guid. It tells the client where along its
         * current course it should be, and the client jumps to it -- so a course that
         * has fallen behind is repaired without resending its geometry. Retail sends it
         * only during flights (484 of them across the captures, always for the mount
         * creature, the progress values spread evenly over the flight and never once
         * reaching 1.0), but nothing in the packet is about flying.
         *
         * Takes a Leg rather than a Course on purpose: the only thing on the wire is a
         * fraction, so this must serve the spline the server is executing today as
         * readily as a Course.
         *
         * @param leg   The leg the client is already walking.
         * @param now   Server instant to report the progress of.
         * @param mover Whose leg it is.
         */
        void WriteSync(Leg const& leg, Instant now, ActorId mover, WorldPacket& out);

        /**
         * @brief Write the stop form: a position, an id, and nothing else.
         */
        void WriteHalt(Domain const& domain, Vector3 const& where, uint32 id,
                       ActorId mover, WorldPacket& out);
    }
}

#endif // MANGOS_COURSEWIRE_H
