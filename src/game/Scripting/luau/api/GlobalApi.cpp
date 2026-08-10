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

// The functions that belong to no object.

#include "LuaApi.h"
#include "Methods.h"

#include "Log.h"
#include "Map.h"

#include "lua.h"
#include "lualib.h"

namespace scripting
{
    namespace api
    {
        namespace
        {
            /**
             * The map this state belongs to, or nil in the world state.
             *
             * Refused while the scripts are still loading, and that is the
             * one place it must be. The engine takes a single census of which
             * events anybody registered for, from the world state, because
             * every state runs the same bytecode -- so
             *
             *     if GetCurrentMap() then OnEvent(...) end
             *
             * would register in every map state and in none of the census,
             * and the handler would never be dispatched to, with nothing
             * logged anywhere. Answering nil here would keep that silent; an
             * error names the file and the line.
             */
            int Lua_GetCurrentMap(lua_State* L)
            {
                if (IsLoading(L))
                {
                    luaL_error(L, "GetCurrentMap is not answerable while the "
                                  "scripts are loading: every state runs this "
                                  "same code, so ask inside a handler");
                }

                Api api(L, BoundMapOf(L));
                api.Push(api.BoundMap());
                return 1;
            }

            /**
             * Write to the server log.
             *
             * print() exists and goes to stdout, which on this server is
             * owned by a dedicated writer thread; this is the one that ends
             * up in the log file with everything else that happened.
             */
            int Lua_PrintInfo(lua_State* L)
            {
                sLog.outString("Luau: %s", luaL_checkstring(L, 1));
                return 0;
            }

            int Lua_PrintError(lua_State* L)
            {
                sLog.outError("Luau: %s", luaL_checkstring(L, 1));
                return 0;
            }
        }

        void RegisterGlobals(lua_State* L)
        {
            static luaL_Reg const functions[] =
            {
                { "GetCurrentMap", Lua_GetCurrentMap },
                { "PrintInfo",     Lua_PrintInfo },
                { "PrintError",    Lua_PrintError },
                { nullptr,         nullptr }
            };

            for (luaL_Reg const* entry = functions; entry->name; ++entry)
            {
                lua_pushcfunction(L, entry->func, entry->name);
                lua_setglobal(L, entry->name);
            }
        }
    }
}
