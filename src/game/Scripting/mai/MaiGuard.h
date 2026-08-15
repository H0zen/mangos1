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

#ifndef MANGOS_MAI_GUARD_H
#define MANGOS_MAI_GUARD_H

#include "MaiScript.h"

#include <cstddef>

/**
 * Asking whether a guard holds, without knowing who is being asked.
 *
 * There is one evaluator now and there were about to be three. A rule's guard
 * was answered inside the creature's AI, which is where the states and the
 * victim are; a step's guard is answered in the RUNNER, which deliberately
 * knows nothing about a creature and can therefore be tested to exhaustion
 * with no map and no server; and a sequence the world started has no creature
 * behind it at all. Three copies of "compare a remembered number" is how two
 * of them end up disagreeing about what `instance:6=0` means outside an
 * instance.
 *
 * So the comparison lives here, once, and what it is comparing is behind an
 * interface -- the same shape and for the same reason as Driver in MaiActor.h.
 * MaiRunner keeps its "no world at all" property, MaiCreatureAI keeps its
 * states, and the test harness answers with a table.
 */
namespace mai
{
    struct Sight
    {
        virtual ~Sight() = default;

        /**
         * The number @a guard is asking about.
         *
         * @return false when the question cannot be asked HERE -- outside an
         *         instance, with no creature to remember anything, with no
         *         victim to look at. Not the same as an answer of zero, and
         *         the difference is load-bearing: zero is a real encounter
         *         state (NOT_STARTED), so an unaskable question treated as
         *         zero makes `instance:6=0` fire in the open world.
         */
        virtual bool Ask(Guard const& guard, uint32& held) const = 0;
    };

    /**
     * Whether all of them hold. An empty list holds -- an unguarded step runs,
     * and it runs even where there is nobody to ask.
     *
     * A null @a sight is not "everything holds": it is "nothing can be asked",
     * so a guard that is there fails. That is what a sequence started by the
     * world does with a guard about a creature's memory, and failing closed is
     * the only safe direction -- a step that should not have run is a bug a
     * player finds, and a step that did not run is a bug a log shows.
     */
    inline bool Holds(Guard const* guards, std::size_t count,
                      Sight const* sight)
    {
        for (std::size_t at = 0; at < count; ++at)
        {
            uint32 held = 0;
            if (!sight || !sight->Ask(guards[at], held))
            {
                return false;
            }

            if (!guards[at].Holds(held))
            {
                return false;
            }
        }

        return true;
    }

    /// The guards @a step names, out of the sequence that holds them.
    inline bool Holds(Sequence const& sequence, Step const& step,
                      Sight const* sight)
    {
        if (step.guardCount == 0)
        {
            return true;
        }

        // A window that does not fit is a compile that went wrong, and there
        // is nothing sensible to run: refuse rather than read past the end.
        std::size_t const first = step.guardFirst;
        if (first + step.guardCount > sequence.guards.size())
        {
            return false;
        }

        return Holds(&sequence.guards[first], step.guardCount, sight);
    }
}

#endif //MANGOS_MAI_GUARD_H
