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

#ifndef MANGOS_AURA_SLOT_ALLOCATOR_H
#define MANGOS_AURA_SLOT_ALLOCATOR_H

#include <cstdint>

/**
 * Which visible slot an aura gets, out of the client's two bands.
 *
 * The 2.4.3 client has 56 slots, the first 40 for positive auras and the rest
 * for negative ones, and it reads them out of `UNIT_FIELD_AURA`. Finding a free
 * one is a scan for the first zero -- there is no free list, and there is
 * deliberately no server-side overflow list either: the cap is a client fact
 * and inventing an eighty-slot server would make the two disagree about what a
 * player can see.
 *
 * THE BAND SIZES ARE ARGUMENTS, not constants declared here. They live in
 * SpellAuraDefines.h beside the update fields they describe, and that file
 * knows about the world -- so the numbers come in and the rule stays out.
 * Passing them also makes the exhaustion case writable in a test without
 * standing up a unit with forty buffs on it.
 */
namespace aura
{
    /**
     * No visible slot: the band was full.
     *
     * NOT the same value as "not presented for a slot yet", which the holder
     * carries as the band total. Two sentinels, because they are two different
     * facts and a reader that conflates them cannot tell an aura the client
     * never had room for from one that was never offered.
     *
     * The caller asserts this equals its own NULL_AURA_SLOT.
     */
    static const std::uint8_t NoSlot = 0xFF;

    /**
     * The first free slot in the band @a positive selects.
     *
     * @param occupied     what the client currently holds, one entry per slot;
     *                     zero means free. @a total entries are read.
     * @param total        how many slots there are in all.
     * @param positiveEnd  where the positive band stops and the negative one
     *                     begins.
     *
     * @return the slot, or @ref NoSlot when that band has no room. Exhaustion
     *         is an ordinary outcome rather than an error: the aura still
     *         exists and still affects the unit, the client simply does not
     *         draw it, which is exactly what the retail client did.
     */
    std::uint8_t AllocateSlot(bool positive,
                              std::uint32_t const* occupied,
                              std::uint8_t total,
                              std::uint8_t positiveEnd);
}

#endif //MANGOS_AURA_SLOT_ALLOCATOR_H
