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

#ifndef MANGOS_ELUNA_ENGINE_H
#define MANGOS_ELUNA_ENGINE_H

#ifdef ENABLE_ELUNA

#include "IScriptEngine.h"

namespace scripting
{
    /**
     * The existing Lua engine, seen through the seam.
     *
     * A pure adapter. It owns nothing, and changes nothing about how Eluna is
     * created, keyed or destroyed -- ElunaMgr still does all of that. It
     * resolves the state exactly the way the old inline call sites did,
     * sWorld.GetEluna() for the global scope and Map::GetEluna() for a map,
     * which is what applies compatibility mode. A converted call site therefore
     * fires the same hook, in the same state, as the hand-written one it
     * replaced; that equivalence is the whole point of this step.
     *
     * The switch below covers the events whose call sites have been converted.
     * Anything else returns Continue, so an event that reaches the seam before
     * its adapter arm does is simply not delivered to Lua yet -- never
     * misdelivered. The arms land per subsystem, alongside the call sites.
     */
    class ElunaEngine : public IEngine
    {
    public:
        char const* GetName() const override { return "Eluna"; }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override;
    };
}

#endif /* ENABLE_ELUNA */
#endif //MANGOS_ELUNA_ENGINE_H
