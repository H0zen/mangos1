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

#ifndef MANGOS_MAI_RUNNER_H
#define MANGOS_MAI_RUNNER_H

#include "MaiGuard.h"
#include "MaiScript.h"

#include <cstddef>

/**
 * Advancing a sequence through time.
 *
 * Kept apart from anything that touches the world, and that separation is the
 * whole design of this file. What a sequence does when a tick arrives is
 * decided by three numbers -- where it got to, how much time has passed, and
 * the times on its steps -- and nothing else. So it can be tested to
 * exhaustion with no map, no creature and no server, which is exactly what the
 * DB script schedule cannot be today.
 *
 * The rules it has to get right are not obvious, and each of them is a way a
 * naive loop goes wrong:
 *
 *   * A TICK IS NOT A STEP. Maps update in whatever slices the server gives
 *     them; a 400ms hitch must run every step that came due inside it, in
 *     order, not one and then wait for the next tick. A sequence whose steps
 *     are 100ms apart must not stretch to the length of the server's worst
 *     frame.
 *
 *   * SEVERAL STEPS SHARE A TIME. Rows at the same delay are ordinary -- say
 *     something and turn to face at the same instant -- and all of them are
 *     due together, in the order they were written.
 *
 *   * TIME IS ABSOLUTE FROM THE START. Elapsed accumulates and is compared
 *     against each step's own time; it is never reset per step. Written as
 *     "count down to the next one", a tick longer than one gap loses the
 *     remainder and every later step drifts by it, which on a busy server is
 *     how a timed encounter slowly falls out of sync with its own dialogue.
 *
 *   * A SEQUENCE CAN BE ENDED FROM INSIDE. terminate_script and terminate_cond
 *     stop the run where they are, so the caller must be able to say "stop"
 *     between two due steps and have the rest of that same tick dropped.
 *
 * AND IT IS ALSO WHERE THE PROGRAM COUNTER LIVES, which is why the control
 * flow was put here rather than beside the verbs. `Frame::next` has always
 * been a program counter -- Next() incremented it, Stop() drove it to the end
 * -- and a jump is that same field written with a different number. A step
 * that branches therefore never reaches Execute at all: it is resolved in the
 * loop below, the frame moves, and the caller is handed the next step that
 * actually does something. Two consequences worth stating:
 *
 *   * The whole of the control flow stays testable with no map, no creature
 *     and no server. What a branch needs from the world is one question --
 *     does this guard hold -- and that arrives through Sight, which the test
 *     harness answers from a table.
 *
 *   * A verb cannot be a branch by accident. Nothing in MaiPerform can move
 *     the frame, because MaiPerform is never given it.
 *
 * THE FUEL IS NOT OPTIONAL. mangosd has one world thread; a loop whose body
 * takes no time would spin in this function and take the server with it. So a
 * frame gets MaxStepsPerTick and no more, and running out yields the tick
 * rather than ending the sequence -- a runaway loop becomes a creature that is
 * busy, visible in the log, and survivable. Every other guarantee here is about
 * fidelity; this one is about the server still being there.
 */
namespace mai
{
    /**
     * Walks the steps that have come due, one call at a time.
     *
     * Used as a loop rather than a callback, because a step's effect may end
     * the sequence and a callback would have to carry that back out anyway:
     *
     * @code
     *     Runner run(frame, diff);
     *     while (Step const* step = run.Next())
     *     {
     *         if (!Perform(*step)) { run.Stop(); }
     *     }
     * @endcode
     */
    class Runner
    {
        public:
            /// @a diff is the tick, in milliseconds. @a sight answers the
            /// guards, and null means none of them can be answered -- see
            /// MaiGuard.h for why that fails closed rather than open.
            Runner(Frame& frame, uint32 diff, Sight const* sight = nullptr)
                : m_frame(frame), m_sight(sight), m_fuel(MaxStepsPerTick),
                  m_stopped(false), m_exhausted(false)
            {
                m_frame.elapsedMs += diff;
            }

            /**
             * The next step due at or before the current time, or nullptr.
             *
             * Advances the frame as it goes, so a sequence interrupted halfway
             * -- by a stop, or by the caller simply not finishing the loop --
             * resumes at the step after the last one handed out rather than
             * repeating it.
             *
             * A step that branches, and a step whose guards do not hold, are
             * consumed here and never handed out. Only a step that is going to
             * DO something leaves this function.
             */
            Step const* Next()
            {
                while (!m_stopped && !m_frame.Finished())
                {
                    Step const& step = m_frame.sequence->steps[m_frame.next];

                    // Time first, guards second: a step that is not due yet is
                    // not asked about, so a guard costs nothing until the
                    // moment it decides something.
                    if (step.atMs > m_frame.elapsedMs)
                    {
                        return nullptr;
                    }

                    if (m_fuel == 0)
                    {
                        // Out of tick, not out of sequence. The frame keeps
                        // its place and resumes; the caller may say so in the
                        // log, which is how a runaway loop is found.
                        m_exhausted = true;
                        return nullptr;
                    }
                    --m_fuel;

                    bool const held =
                        Holds(*m_frame.sequence, step, m_sight);

                    switch (step.flow)
                    {
                        case FlowSkip:
                            // `if` and `while`. Holding means the block runs,
                            // and the jump is where the block ends.
                            if (held)
                            {
                                ++m_frame.next;
                            }
                            else
                            {
                                GoTo(step.jump);
                            }
                            continue;

                        case FlowAlways:
                            // `else`, `break`, `continue`. A guard on one of
                            // these is the one-line form -- "break if we are
                            // out of adds" -- so a guard that fails means the
                            // jump simply does not happen.
                            if (held)
                            {
                                GoTo(step.jump);
                            }
                            else
                            {
                                ++m_frame.next;
                            }
                            continue;

                        case FlowEnter:
                            // `repeat n`. Zero turns, or a guard that does not
                            // hold, and the loop is skipped outright.
                            if (held && step.loopSlot < MaxLoopDepth &&
                                step.operands[0].u > 0)
                            {
                                m_frame.loopCount[step.loopSlot] =
                                    step.operands[0].u;
                                ++m_frame.next;
                            }
                            else
                            {
                                GoTo(step.jump);
                            }
                            continue;

                        case FlowLoop:
                        {
                            // The `end` of a `repeat`: one turn off, and back
                            // to the top while any are left. Guards are refused
                            // on this one at load, so `held` is not consulted.
                            uint32& left =
                                m_frame.loopCount[step.loopSlot < MaxLoopDepth
                                                      ? step.loopSlot : 0];
                            if (left > 0)
                            {
                                --left;
                            }

                            if (left > 0)
                            {
                                GoTo(step.jump);
                            }
                            else
                            {
                                ++m_frame.next;
                            }
                            continue;
                        }

                        case FlowNone:
                        default:
                            break;
                    }

                    ++m_frame.next;

                    // An ordinary step whose guards do not hold is skipped,
                    // not jumped: that is `if` for one line, and it is the
                    // same field doing the same thing without the block.
                    if (!held)
                    {
                        continue;
                    }

                    return &step;
                }

                return nullptr;
            }

            /// End the run here. Whatever else was due this tick is dropped,
            /// which is what terminate_script means.
            void Stop()
            {
                m_stopped = true;
                m_frame.next = m_frame.sequence
                                   ? m_frame.sequence->steps.size()
                                   : 0;
            }

            bool Stopped() const { return m_stopped; }

            /// Whether this tick ended because the frame ran out of steps
            /// rather than out of work. Worth a line in the log: it means a
            /// loop in a table is not getting anywhere.
            bool Exhausted() const { return m_exhausted; }

        private:
            /**
             * Move the frame, and put the clock where the destination expects
             * to find it.
             *
             * FORWARD is nothing but an assignment: the steps jumped over were
             * due or not due on their own times, and skipping them changes
             * neither the elapsed time nor anybody else's.
             *
             * BACKWARD is a new turn of a loop, and the clock has to come back
             * with it or the second turn finds every one of its steps already
             * overdue and runs the whole body in one tick. What comes off is
             * the loop's PERIOD -- the time on the `end` less the time on the
             * step it goes back to -- so an author writes the shape of one turn
             * and gets it repeated:
             *
             *     seq  at_ms  action                params
             *     0        0  repeat                times=3
             *     1        0  cast_spell            spell=11962
             *     2     1000  end
             *
             * three casts, a second apart. Subtracting the period rather than
             * assigning the head's time is what keeps the REMAINDER of a long
             * tick, so a hundred turns do not drift by a hundred hitches --
             * the same rule this file has always applied to a straight line.
             */
            void GoTo(uint16 target)
            {
                if (m_frame.sequence &&
                    std::size_t(target) <= m_frame.next &&
                    std::size_t(target) < m_frame.sequence->steps.size())
                {
                    uint32 const from =
                        m_frame.sequence->steps[m_frame.next].atMs;
                    uint32 const to = m_frame.sequence->steps[target].atMs;
                    uint32 const period = from > to ? from - to : 0;

                    m_frame.elapsedMs = m_frame.elapsedMs > period
                                            ? m_frame.elapsedMs - period
                                            : 0;
                }

                m_frame.next = target;
            }

            Frame&       m_frame;
            Sight const* m_sight;
            uint32       m_fuel;
            bool         m_stopped;
            bool         m_exhausted;
    };

    /**
     * How long until this frame next needs a tick, or NeverMs when never.
     *
     * A schedule that knows this can leave a frame alone instead of asking it
     * every tick whether anything happened -- which, for the several hundred
     * sequences a populated instance can have pending, is the difference
     * between a lookup and a walk.
     */
    enum : uint32 { NeverMs = 0xFFFFFFFFu };

    inline uint32 UntilNextMs(Frame const& frame)
    {
        if (frame.Finished())
        {
            return NeverMs;
        }

        uint32 const due = frame.NextDueMs();
        return due > frame.elapsedMs ? due - frame.elapsedMs : 0;
    }
}

#endif //MANGOS_MAI_RUNNER_H
