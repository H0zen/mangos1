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

#ifndef MANGOS_MAI_SELECT_H
#define MANGOS_MAI_SELECT_H

#include "MaiScript.h"

class Creature;
class Unit;

/**
 * Turning a Selector into the unit it names, at the moment the step runs.
 *
 * Kept apart from MaiTargeting for a reason that is not filing: the
 * rearrangement there is pure logic over three handles and is tested with
 * integers. This is not pure and cannot be -- "a random player on my threat
 * list" is a question only a live threat manager can answer, and the answer
 * differs between two ticks of the same sequence. Mixing the two would have
 * cost the half that IS testable its testability.
 */
namespace mai
{
    /**
     * What the trigger knew, carried through to the steps it started.
     *
     * A rule fires BECAUSE something happened, and half the selectors ask
     * about that something: who hit us, who sent the event. The DB scripts
     * never needed this -- a queued command list is told its target when it is
     * queued -- which is why it arrives with the rules and not before them.
     */
    struct Invocation
    {
        Unit*     invoker = nullptr;    ///< who made the rule fire
        Creature* sender = nullptr;     ///< who threw the AI event, if one did
    };

    /**
     * The unit @a select names, or nullptr.
     *
     * @param missing set when the selector names someone who is not there: an
     *        empty threat list, a rule with no invoker. EventAI reported this
     *        per action and skipped the action, and so does the caller -- it is
     *        an ordinary occurrence (a boss with one player left on the list
     *        has no "second highest threat") and not a script error.
     * @param forSpellId when the step is a cast, so a target that cannot be
     *        hit by that spell is not chosen. Zero means "any".
     */
    Unit* Select(Creature* self, Selector select, Invocation const& from,
                 bool& missing, uint32 forSpellId = 0, uint32 selectFlags = 0);
}

#endif //MANGOS_MAI_SELECT_H
