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

#ifndef MANGOS_LUAU_API_METHODS_H
#define MANGOS_LUAU_API_METHODS_H

#include "LuaApi.h"

#include <cstddef>

namespace scripting
{
    namespace api
    {
        /**
         * One table per type, each in its own translation unit.
         *
         * Eluna kept all of these in headers included by a single Methods.cpp,
         * which made that one file half a megabyte of template instantiation
         * and the slowest thing in its build. They are compiled units here
         * instead, so a change to how a creature answers does not recompile
         * how a guild does.
         */
        MethodEntry const* ObjectMethods(std::size_t& count);
        MethodEntry const* WorldObjectMethods(std::size_t& count);
        MethodEntry const* UnitMethods(std::size_t& count);
        MethodEntry const* PlayerMethods(std::size_t& count);
        MethodEntry const* CreatureMethods(std::size_t& count);
        MethodEntry const* GameObjectMethods(std::size_t& count);
        MethodEntry const* ItemMethods(std::size_t& count);
        MethodEntry const* CorpseMethods(std::size_t& count);
        MethodEntry const* MapMethods(std::size_t& count);
        MethodEntry const* GroupMethods(std::size_t& count);
        MethodEntry const* GuildMethods(std::size_t& count);
        MethodEntry const* QuestMethods(std::size_t& count);
        MethodEntry const* SpellMethods(std::size_t& count);
        MethodEntry const* AuraMethods(std::size_t& count);
        MethodEntry const* PacketMethods(std::size_t& count);
        MethodEntry const* BattleGroundMethods(std::size_t& count);

        /// The functions that belong to no object: lookups, the databases,
        /// the clock, the server itself.
        void RegisterGlobals(lua_State* L);
    }
}

#endif //MANGOS_LUAU_API_METHODS_H
