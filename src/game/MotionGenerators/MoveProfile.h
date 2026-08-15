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

#ifndef MANGOS_MOVEPROFILE_H
#define MANGOS_MOVEPROFILE_H

#include "nav/NavArea.hpp"

class Unit;

namespace Nav
{
    /**
     * @brief Snapshot what `mover` may do right now.
     *
     * THE one place that reads a Unit in order to decide movement policy. The router
     * itself lives in src/shared/nav and has never heard of a Unit; everything it needs
     * to know about the mover arrives as the value this returns.
     *
     * It is a snapshot and not a cache: several of the permissions depend on where the
     * mover is standing at this instant -- the liquid exemption below is recomputed
     * every time -- so it is taken per routing request rather than once per mover.
     */
    MoveProfile ProfileOf(Unit const& mover);
}

#endif // MANGOS_MOVEPROFILE_H
