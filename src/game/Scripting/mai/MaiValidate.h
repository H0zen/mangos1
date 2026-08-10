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


#ifndef MANGOS_MAI_VALIDATE_H
#define MANGOS_MAI_VALIDATE_H

#include "MaiScript.h"

#include <cstddef>
#include <string>

/**
 * Holding a script up against the world before it is allowed to run.
 *
 * The one thing a table can do that a program cannot, and the reason the
 * manifest gave every parameter a semantic type instead of calling them all
 * `datalong`.
 */
namespace mai
{
    /// @return false with @a error naming the parameter and what it should be.
    bool Validate(Step const& step, std::string& error);

    /// Every step, each reported through the DB error log. @return how many
    /// were refused, which is a number worth printing at the end of a load.
    std::size_t Validate(Sequence const& sequence);
}

#endif //MANGOS_MAI_VALIDATE_H
