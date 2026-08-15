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

#ifndef MANGOS_MAI_TARGETING_H
#define MANGOS_MAI_TARGETING_H

#include "MaiScript.h"

/**
 * Who a step ends up acting on.
 *
 * This is the subtlest thing the DB scripts do and the place a reimplementation
 * is most likely to be quietly wrong. A row starts with a source and a target,
 * may find a third object nearby, and then four flags rearrange the three of
 * them -- IN ORDER, each acting on the result of the last. Written as one
 * function mixing the search with the rearrangement, as it is today, the
 * rearrangement can only be checked by running a server and looking.
 *
 * So it is two things here. FINDING the buddy needs a map, a grid and a
 * searcher. DECIDING what the three objects become given the flags is pure
 * logic over three values, and pure logic can be enumerated: there are eight
 * meaningful flag combinations and a test walks all of them.
 *
 * The order below is the original's and must stay it. Swapping the reverse and
 * the self-target steps, for instance, changes the answer for a row that sets
 * both -- and roughly one row in thirty on a live world sets more than one.
 */
namespace mai
{
    /// The data_flags a row carries, named. The values are the DB's own.
    enum TargetFlags : uint8
    {
        BuddyAsTarget    = 0x01,    ///< act on the buddy rather than through it
        ReverseDirection = 0x02,    ///< swap whatever source and target are now
        SourceTargetsSelf= 0x04,    ///< the target becomes the source
        CommandAdditional= 0x08,    ///< meaning depends on the verb
        BuddyByGuid      = 0x10,    ///< the buddy field is a guid, not a search
        BuddyIsPet       = 0x20,
        BuddyIsDespawned = 0x40,    ///< look among the dead, not the living

        /// Any one of them, not the nearest. The search already finds every
        /// creature of an entry within a radius and then throws all but the
        /// closest away; this keeps them and picks one.
        ///
        /// It is what "explode one of the adds" means, and the C++ that says
        /// it reads a guid out of an instance's own list -- which looked like
        /// MAI needing to read lists, and was really MAI needing to say
        /// "any of them".
        BuddyRandom      = 0x80,
    };

    /**
     * One step's cast of characters, before and after the flags are applied.
     *
     * Deliberately templated on nothing and holding no pointers: it is three
     * opaque handles, so the rearrangement can be tested with integers and
     * still be the very code that runs with objects.
     */
    template <class T>
    struct Cast
    {
        T source = T();
        T target = T();
        T buddy  = T();     ///< what the search found, or nothing
    };

    /**
     * Apply the four rearranging flags, in the order the DB scripts apply them.
     *
     * @return the final (source, target). The buddy is consumed: after this it
     *         is either one of the two or it is not used at all.
     */
    template <class T>
    void Redirect(uint8 flags, Cast<T> const& cast, T& source, T& target)
    {
        if (flags & BuddyAsTarget)
        {
            // s -> b : the source keeps acting, but on the buddy.
            source = cast.source;
            target = cast.buddy;
        }
        else
        {
            // s/b -> t : the buddy REPLACES the source when there is one,
            // which is the default and the reason most rows need no flag at
            // all. A row with a buddy and no flags means "have that creature
            // over there do this".
            source = cast.buddy != T() ? cast.buddy : cast.source;
            target = cast.target;
        }

        if (flags & ReverseDirection)
        {
            T const held = source;
            source = target;
            target = held;
        }

        if (flags & SourceTargetsSelf)
        {
            target = source;
        }
    }
}

#endif //MANGOS_MAI_TARGETING_H
