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

#ifndef MANGOS_MAI_EXECUTE_H
#define MANGOS_MAI_EXECUTE_H

#include "MaiActor.h"
#include "MaiScript.h"
#include "MaiSelect.h"

#include "ObjectGuid.h"

class Map;

/**
 * Carrying out one step, whatever started it.
 *
 * This lived inside MaiEngine.cpp while there was exactly one thing that ran
 * steps. There are two now -- a sequence the world started, and a rule a
 * creature's AI fired -- and they must not be two answers to "what does this
 * step do". The buddy search, the four rearranging flags, the native bodies
 * and the borrowed ones are one path here, and the two callers differ only in
 * what they put in the Run.
 */
namespace mai
{
    /**
     * Everything a step needs that is not the step.
     *
     * Guids rather than pointers, resolved on entry, for the reason the whole
     * model gives: a run happens over time and anything it names can die in the
     * middle of it. The Invocation is the exception and is deliberately raw --
     * the caller resolves it from its own guids immediately before calling, so
     * it is never older than this one call.
     */
    struct Run
    {
        Map*       map = nullptr;

        ObjectGuid source;
        ObjectGuid target;
        ObjectGuid owner;       ///< the player holding an item source

        Invocation from;        ///< what the trigger knew, for the selectors

        /// The creature's own state, when a creature is running this. Null for
        /// a sequence the world started, which has no creature behind it.
        Actor*     actor = nullptr;

        /// Its AI, for the verbs that can only be carried out there. Null for
        /// the same reason and in the same cases.
        Driver*    driver = nullptr;

        /// Which `db_scripts` type the borrowed bodies should think they are.
        /// Migration scaffolding, and it leaves with them.
        uint32     origin = 0;

        /// Set when a step's cast was refused, so the rule that started it can
        /// decide to try again sooner. Null when nobody is listening, which is
        /// every sequence the world starts.
        bool*      refused = nullptr;

        /// The item whose use started this, when one did.
        ObjectGuid item;

        /// Set by `refuse_use`. Null unless somebody is waiting on the answer,
        /// which is only ever an inline run.
        bool*      cancel = nullptr;

        /// Whether the step's Selector means anything.
        ///
        /// A sequence lowered from `dbscripts_on_*` has no selectors, and its
        /// zero in that column reads as SelectSelf rather than as "none" --
        /// the two cannot be told apart from the step alone, so the caller
        /// says which it is. Rules set it, and so do the kinds whose steps
        /// were written rather than converted.
        bool       useSelectors = false;

        /// What the moment that started this carried, for a step that
        /// computes its own numbers. Copied from the frame each tick.
        Combat::PointsInputs numbers;
    };

    /**
     * @return true when the sequence should stop at this step.
     */
    bool Execute(Run const& run, Step const& step);
}

#endif //MANGOS_MAI_EXECUTE_H
