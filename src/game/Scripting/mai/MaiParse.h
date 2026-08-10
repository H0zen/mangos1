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

#ifndef MANGOS_MAI_PARSE_H
#define MANGOS_MAI_PARSE_H

#include "MaiScript.h"

#include <string>

/**
 * Reading a step back out of the table.
 *
 * `params` is "name=value name=value", with every name and type taken from
 * actions.manifest. That is what lets the same column be readable to a person
 * and checkable by a machine: a misspelt parameter is a load error naming the
 * parameter and the verb, and so is a value of the wrong shape. Neither is
 * expressible in six columns called datalong.
 */
namespace mai
{
    /// The verb @a name names, or ActionId::None.
    ActionId ActionNamed(char const* name);

    /// @return false with @a error naming what is wrong and where. Strict: an
    ///         unknown parameter is refused rather than ignored.
    bool Parse(char const* action, char const* params, Step& out,
               std::string& error);
}

#endif //MANGOS_MAI_PARSE_H
