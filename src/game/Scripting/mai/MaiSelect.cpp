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

// Resolving a selector against the world.
//
// A transcription of EventAI's GetTargetByType, and deliberately a close one:
// the eleven cases are not obvious, several of them differ only in a `1` where
// another has a `0`, and the conditions under which each reports "nobody" are
// stranger still. TARGET_T_HOSTILE_SECOND_AGGRO complains when the threat list
// has more than one entry and it still found nothing, but says nothing when
// the list has exactly one -- because a creature fighting a single player
// HAS no second-highest, and reporting that every two seconds for the length
// of the fight is how a log becomes unreadable.
//
// Reproducing that quiet, rather than tidying it into a uniform "not found",
// is the difference between a migration and a rewrite. The rewrite can come
// afterwards, with this to compare against.

#include "MaiSelect.h"

#include "Creature.h"
#include "Unit.h"

namespace mai
{
    Unit* Select(Unit* self, Selector select, Invocation const& from,
                 bool& missing, uint32 forSpellId, uint32 selectFlags)
    {
        Unit* found = nullptr;

        // The four that need no threat list. Answered first so that they work
        // for a source that is not a creature at all.
        switch (select)
        {
        case SelectSelf:
            if (!self)
            {
                missing = true;
            }
            return self;

        case SelectInvoker:
            if (!from.invoker)
            {
                missing = true;
            }
            return from.invoker;

        case SelectInvokerOwner:
            found = from.invoker ? from.invoker->GetCharmerOrOwnerOrSelf()
                                 : nullptr;
            if (!found)
            {
                missing = true;
            }
            return found;

        case SelectEventSender:
            if (!from.sender)
            {
                missing = true;
            }
            return from.sender;

        case SelectSelfOwner:
            found = self ? self->GetCharmerOrOwner() : nullptr;
            if (!found)
            {
                missing = true;
            }
            return found;

        case SelectRandomUnfriendly:
            found = self ? self->SelectRandomUnfriendlyTarget() : nullptr;
            if (!found)
            {
                missing = true;
            }
            return found;

        default:
            break;
        }

        // Everything below reads a threat list, which only a creature has.
        Creature* creature = self ? creature->ToCreature() : nullptr;
        if (!creature)
        {
            missing = true;
            return nullptr;
        }

        switch (select)
        {
        case SelectVictim:
            found = creature->getVictim();
            if (!found)
            {
                missing = true;
            }
            return found;

        case SelectSecondAggro:
            found = creature->SelectAttackingTarget(ATTACKING_TARGET_TOPAGGRO, 1,
                                                forSpellId, selectFlags);
            // Silent when the list holds exactly one: there is no second, and
            // saying so on every tick of a two-minute fight is noise.
            if (!found &&
                ((forSpellId == 0 && selectFlags == 0 &&
                  creature->GetThreatManager().getThreatList().size() > 1) ||
                 creature->GetThreatManager().getThreatList().empty()))
            {
                missing = true;
            }
            return found;

        case SelectLastAggro:
            found = creature->SelectAttackingTarget(ATTACKING_TARGET_BOTTOMAGGRO, 0,
                                                forSpellId, selectFlags);
            if (!found && creature->GetThreatManager().getThreatList().empty())
            {
                missing = true;
            }
            return found;

        case SelectRandom:
            found = creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0,
                                                forSpellId, selectFlags);
            if (!found && creature->GetThreatManager().getThreatList().empty())
            {
                missing = true;
            }
            return found;

        case SelectRandomNotTop:
            found = creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 1,
                                                forSpellId, selectFlags);
            if (!found &&
                ((forSpellId == 0 && selectFlags == 0 &&
                  creature->GetThreatManager().getThreatList().size() > 1) ||
                 creature->GetThreatManager().getThreatList().empty()))
            {
                missing = true;
            }
            return found;

        case SelectRandomPlayer:
            found = creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0,
                                                forSpellId,
                                                SELECT_FLAG_PLAYER | selectFlags);
            // Unlike its non-player twin this always complains, which is the
            // original's own asymmetry: a script that asked for a player and
            // got none is usually a script running on the wrong pull.
            if (!found)
            {
                missing = true;
            }
            return found;

        case SelectRandomPlayerNotTop:
            found = creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 1,
                                                forSpellId,
                                                SELECT_FLAG_PLAYER | selectFlags);
            if (!found &&
                ((forSpellId == 0 && selectFlags == 0 &&
                  creature->GetThreatManager().getThreatList().size() > 1) ||
                 creature->GetThreatManager().getThreatList().empty()))
            {
                missing = true;
            }
            return found;

        default:
            missing = true;
            return nullptr;
        }
    }
}
