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

#ifndef MANGOS_ISCRIPT_ENGINE_H
#define MANGOS_ISCRIPT_ENGINE_H

#include "ScriptHost.h"

/**
 * One scripting back end.
 *
 * Nothing in src/game outside the Scripting directory includes this header,
 * and nothing needs to: an engine is reached only through Dispatch(). Adding a
 * second back end -- another interpreter, or the C++ scripts, which are a back
 * end like any other -- is a new implementation of this interface plus one
 * line in ScriptHost.cpp, not an edit anywhere in the world.
 */
namespace scripting
{
    class IEngine
    {
    public:
        virtual ~IEngine() = default;

        /// For log lines and console output; never parsed.
        virtual char const* GetName() const = 0;

        /**
         * Run @a id in the state named by @a ctx.
         *
         * @a args is the caller's payload and may be written back into, but a
         * slot must keep the Kind it arrived with.
         *
         * An engine swallows its own failures: a script error is logged and
         * reported as Verdict::Continue. Nothing from a script may unwind
         * through the world tick.
         */
        virtual Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                                 std::size_t count) = 0;
    };
}

#endif //MANGOS_ISCRIPT_ENGINE_H
