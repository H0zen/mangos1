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

#ifndef MANGOS_COMBAT_PROCREGISTRY_H
#define MANGOS_COMBAT_PROCREGISTRY_H

#include "ProcEvent.h"

#include <cstdint>

namespace Combat
{
    /// A proc handler. A free function over a value, not a method on a unit.
    using ProcHandler = ProcResult (*)(ProcEvent const&);

    /**
     * @brief The handler for an aura type.
     *
     * Never null. An aura type with nothing registered against it answers Ok
     * without doing anything, so the caller has no branch to remember.
     *
     * O(1). The registration list is sparse -- 23 aura types out of 262 have
     * a handler at all -- and is expanded once into a dense array behind this
     * call.
     */
    ProcHandler ProcHandlerFor(std::uint32_t auraType);

    /// How many aura types carry a handler. For the boot log and the tests.
    std::uint32_t RegisteredProcHandlerCount();
}

#endif
