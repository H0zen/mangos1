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
            /// @a diff is the tick, in milliseconds.
            Runner(Frame& frame, uint32 diff)
                : m_frame(frame), m_stopped(false)
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
             */
            Step const* Next()
            {
                if (m_stopped || m_frame.Finished())
                {
                    return nullptr;
                }

                Step const& step = m_frame.sequence->steps[m_frame.next];
                if (step.atMs > m_frame.elapsedMs)
                {
                    return nullptr;
                }

                ++m_frame.next;
                return &step;
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

        private:
            Frame& m_frame;
            bool   m_stopped;
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
