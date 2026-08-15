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

// Blocks into jumps.
//
// One pass with a stack, which is all a language of three structures needs.
// Every block verb is met once, on the way down; the jumps that point FORWARD
// -- past an `else`, past an `end` -- are not known until the block closes, so
// they are written then, by the `end`, into the rows that are waiting for them.
// Nothing needs a second pass because nothing can jump into a block from
// outside it: there is no goto and no label, deliberately.
//
// TIME IS THE PART THAT IS NOT OBVIOUS. `at_ms` has always meant "from the
// start of the sequence", and the runner gates each step against it. A jump
// leaves that alone in one direction and cannot in the other:
//
//   * FORWARD, the clock is untouched. A block occupies its stretch of the
//     timeline whichever arm of it runs, so an `if` whose `then` takes five
//     seconds and whose `else` takes none still reaches the step after the
//     block at five seconds. That is the cutscene reading, it is what the
//     timeline already meant, and it is printable without being run.
//
//   * BACKWARD is a new TURN of a loop, so the clock comes back with it by the
//     turn's period -- see Runner::GoTo. Which means the times inside a loop
//     body are the times of ONE turn, and the check below has to exempt the
//     back edge from wanting them to increase.
//
// So what is checked here is that time never runs backwards along any edge
// that is not a loop's back edge. A step that would be due before the one that
// can reach it is a row somebody mistyped, and without the check it silently
// runs the instant it is reached rather than when it says.

#include "MaiCompile.h"

#include <cstdio>

namespace mai
{
    namespace
    {
        /// Not an index any step has. `std::size_t(-1)` rather than NoJump,
        /// because this is bookkeeping in the compiler and NoJump is a value
        /// that ends up in a step.
        std::size_t const NoStep = std::size_t(-1);

        /// One block still open, and everything the `end` will have to know.
        struct Open
        {
            ActionId    kind = ActionId::None;  ///< If, While or Repeat
            std::size_t head = 0;
            std::size_t elseAt = NoStep;
            uint8       slot = 0;               ///< a Repeat's loop counter

            std::vector<std::size_t> breaks;
            std::vector<std::size_t> continues;
        };

        char const* NameOf(ActionId id)
        {
            ActionSpec const* spec = SpecOf(id);
            return spec ? spec->name : "?";
        }

        void Say(std::string& error, char const* what, std::size_t at)
        {
            char buffer[192];
            std::snprintf(buffer, sizeof(buffer), "step %u: %s",
                          uint32(at), what);
            error = buffer;
        }
    }

    bool IsControl(ActionId id)
    {
        switch (id)
        {
            case ActionId::If:
            case ActionId::Else:
            case ActionId::End:
            case ActionId::Repeat:
            case ActionId::While:
            case ActionId::Break:
            case ActionId::Continue:
                return true;

            default:
                return false;
        }
    }

    bool Branches(std::vector<Step> const& steps)
    {
        for (Step const& step : steps)
        {
            if (IsControl(step.action))
            {
                return true;
            }
        }

        return false;
    }

    bool Compile(Sequence& sequence, std::string& error)
    {
        std::vector<Step>& steps = sequence.steps;

        sequence.program = Branches(steps);
        if (!sequence.program)
        {
            // A timeline, and it stays one. Nothing to resolve, nothing to
            // check, and -- most of all -- nothing changed about how the
            // 27,561 steps a converted world already has are read.
            return true;
        }

        // A jump is a uint16 so that a step stays small, which puts a ceiling
        // on how long a program may be. Said out loud rather than truncated:
        // a silently wrapped jump target is a branch into the middle of
        // somewhere else.
        if (steps.size() >= NoJump)
        {
            Say(error, "a script that branches may not be this long", 0);
            return false;
        }

        std::vector<Open> open;
        uint8 depth = 0;    ///< how many `repeat`s are open, for the slots

        for (std::size_t at = 0; at < steps.size(); ++at)
        {
            Step& step = steps[at];

            switch (step.action)
            {
                case ActionId::If:
                case ActionId::While:
                {
                    step.flow = FlowSkip;

                    Open block;
                    block.kind = step.action;
                    block.head = at;
                    open.push_back(block);
                    break;
                }

                case ActionId::Repeat:
                {
                    if (depth >= MaxLoopDepth)
                    {
                        // Every level costs a counter on every running frame,
                        // and the slot is handed out here rather than pushed
                        // at run time -- so the ceiling is real and this is
                        // where it is met.
                        Say(error, "loops nest deeper than MAI allows", at);
                        return false;
                    }

                    step.flow = FlowEnter;
                    step.loopSlot = depth;

                    Open block;
                    block.kind = ActionId::Repeat;
                    block.head = at;
                    block.slot = depth;
                    open.push_back(block);
                    ++depth;
                    break;
                }

                case ActionId::Else:
                {
                    if (open.empty() || open.back().kind != ActionId::If)
                    {
                        Say(error, "an 'else' with no 'if' open", at);
                        return false;
                    }

                    if (open.back().elseAt != NoStep)
                    {
                        Say(error, "a second 'else' on one 'if'", at);
                        return false;
                    }

                    if (step.guardCount != 0)
                    {
                        // The condition belongs on the `if`. An `else` that
                        // could also test something is an `else if` spelt so
                        // that half the rows deciding one branch are in two
                        // different places.
                        Say(error, "'else' takes no guard; put it on the 'if' "
                                   "or nest another one", at);
                        return false;
                    }

                    open.back().elseAt = at;
                    step.flow = FlowAlways;
                    break;
                }

                case ActionId::Break:
                case ActionId::Continue:
                {
                    std::size_t which = open.size();
                    while (which > 0)
                    {
                        ActionId const kind = open[which - 1].kind;
                        if (kind == ActionId::Repeat ||
                            kind == ActionId::While)
                        {
                            break;
                        }
                        --which;
                    }

                    if (which == 0)
                    {
                        char buffer[96];
                        std::snprintf(buffer, sizeof(buffer),
                                      "'%s' with no loop around it",
                                      NameOf(step.action));
                        Say(error, buffer, at);
                        return false;
                    }

                    step.flow = FlowAlways;

                    if (step.action == ActionId::Break)
                    {
                        open[which - 1].breaks.push_back(at);
                    }
                    else
                    {
                        open[which - 1].continues.push_back(at);
                    }
                    break;
                }

                case ActionId::End:
                {
                    if (open.empty())
                    {
                        Say(error, "an 'end' with nothing open", at);
                        return false;
                    }

                    if (step.guardCount != 0)
                    {
                        Say(error, "'end' takes no guard", at);
                        return false;
                    }

                    Open const block = open.back();
                    open.pop_back();

                    // Where a `break` goes, and where the block is done.
                    std::size_t const after = at + 1;

                    if (block.kind == ActionId::If)
                    {
                        // The `end` of an `if` does nothing except not be
                        // handed to the world: a control verb must never
                        // reach Execute, so it moves the frame by one rather
                        // than being an ordinary step somebody has to write a
                        // body for.
                        step.flow = FlowAlways;
                        step.jump = uint16(after);

                        if (block.elseAt != NoStep)
                        {
                            steps[block.head].jump = uint16(block.elseAt + 1);
                            steps[block.elseAt].jump = uint16(after);
                        }
                        else
                        {
                            steps[block.head].jump = uint16(after);
                        }
                    }
                    else if (block.kind == ActionId::While)
                    {
                        // Back to the condition, which is the whole of what
                        // makes it a `while` rather than a `repeat`.
                        step.flow = FlowAlways;
                        step.jump = uint16(block.head);
                        steps[block.head].jump = uint16(after);
                    }
                    else
                    {
                        // Back to the first step of the BODY, not to the
                        // `repeat` itself -- which would reload the counter
                        // and never finish.
                        step.flow = FlowLoop;
                        step.loopSlot = block.slot;
                        step.jump = uint16(block.head + 1);
                        steps[block.head].jump = uint16(after);

                        if (depth > 0)
                        {
                            --depth;
                        }
                    }

                    for (std::size_t leaving : block.breaks)
                    {
                        steps[leaving].jump = uint16(after);
                    }

                    // A `continue` in a `while` goes back to the condition; in
                    // a `repeat` it goes to the `end`, because that is where
                    // the counter comes down. Sent to the top instead, a
                    // `continue` would make a bounded loop unbounded.
                    for (std::size_t again : block.continues)
                    {
                        steps[again].jump =
                            uint16(block.kind == ActionId::While
                                       ? block.head : at);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        if (!open.empty())
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "'%s' is never closed",
                          NameOf(open.back().kind));
            Say(error, buffer, open.back().head);
            return false;
        }

        // Time, along every edge that is not a loop's way back. See the head
        // of this file for why the back edge is exempt and nothing else is.
        for (std::size_t at = 0; at < steps.size(); ++at)
        {
            Step const& step = steps[at];

            // A FlowAlways step falls through only when it carries a guard --
            // that is the one-line `break if`, and its other way out is the
            // very next row. `else` and the `end` of an `if` may not carry
            // one, so for those the jump really is the only edge.
            bool const canFallThrough =
                step.flow != FlowAlways || step.guardCount != 0;

            std::size_t const targets[2] =
            {
                canFallThrough ? at + 1 : NoStep,
                step.jump == NoJump ? NoStep : std::size_t(step.jump)
            };

            for (std::size_t to : targets)
            {
                if (to == NoStep || to <= at || to >= steps.size())
                {
                    continue;
                }

                if (steps[to].atMs < step.atMs)
                {
                    char buffer[160];
                    std::snprintf(buffer, sizeof(buffer),
                                  "runs at %ums, after a step at %ums that "
                                  "reaches it; time cannot go backwards except "
                                  "round a loop",
                                  steps[to].atMs, step.atMs);
                    Say(error, buffer, to);
                    return false;
                }
            }
        }

        return true;
    }
}
