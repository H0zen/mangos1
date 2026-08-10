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

#ifndef MANGOS_DBSCRIPT_ENGINE_H
#define MANGOS_DBSCRIPT_ENGINE_H

#include "IScriptEngine.h"

namespace scripting
{
    /**
     * The `dbscripts_on_*` tables, seen through the seam.
     *
     * The oldest scripting system in the tree and the first one to become an
     * engine. It is worth being precise about what it is, because it does not
     * look like the others: a DB script is a list of commands with per-command
     * delays, and starting one only APPENDS to the map's schedule. Nothing
     * runs before this call returns, and Map::ScriptsProcess picks the work up
     * on a later tick.
     *
     * Three consequences, all of them properties of the engine rather than
     * choices made here:
     *
     *  - It can never cancel or claim. There is no answer to give while the
     *    commands have not run, so every dbscript event is `broadcast` in the
     *    manifest and this Dispatch only ever returns Continue.
     *  - It is per-map by construction. The schedule lives on Map, so an event
     *    that arrives with no map context is not for this engine.
     *  - `started` is a value, not a verdict. ScriptsStart reports whether it
     *    queued anything -- one call site logs an error when it did not -- and
     *    that travels back in the payload slot rather than in the Verdict,
     *    which means something else entirely.
     *
     * It bids for no roles: DB scripts drive nothing, they are fired at things.
     */
    class DbScriptEngine : public IEngine
    {
    public:
        char const* GetName() const override { return "DBScripts"; }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override;

        void LoadData(LoadPhase phase) override;
        bool ReloadData(char const* table) override;
    };
}

#endif //MANGOS_DBSCRIPT_ENGINE_H
