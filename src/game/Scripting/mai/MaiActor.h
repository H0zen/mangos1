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

#ifndef MANGOS_MAI_ACTOR_H
#define MANGOS_MAI_ACTOR_H

#include "MaiScript.h"

#include "ObjectGuid.h"
#include "Platform/Define.h"

class Creature;
class Map;
class Unit;
class WorldObject;

/**
 * What one creature running MAI remembers between ticks.
 *
 * A sequence needs none of this -- it is a list of things to do and a clock.
 * A creature does: it is in a phase, it may be refusing to die below a health
 * it was told, it has timers that re-arm. EventAI kept all of it as fields on
 * its AI object, which is the right place; what was wrong was that the AI
 * object was also the interpreter, the action set and the trigger table.
 *
 * WHY IT EXISTS BEFORE THE RULE ENGINE DOES. The actions come first -- a rule
 * with nothing to run is not testable -- and several of them write here:
 * set_phase, set_invincibility, set_throw_mask. Building the state with the
 * actions that use it, rather than with the engine that will read it, keeps
 * each piece checkable on its own.
 */
namespace mai
{
    /**
     * Phases, still a bitmask.
     *
     * EventAI's whole notion of state, and it deserves to become named states
     * -- `state combat`, `state frenzy` -- which is what the design says and
     * what a conversion cannot deliver in the same change that moves twenty
     * thousand rows. A rule carries the mask of phases it does NOT fire in,
     * inverted, exactly as the tables have it, so a converted row means what
     * it meant.
     */
    struct Phases
    {
        uint32 current = 0;     ///< the phase NUMBER, 0..31; ZERO is where a
                                ///< creature starts, as EventAI had it -- and
                                ///< a creature that has died is put back to it

        bool Allows(uint32 inversePhaseMask) const
        {
            return inversePhaseMask == 0 ||
                   (inversePhaseMask & (1u << current)) == 0;
        }
    };

    /**
     * One creature's MAI state.
     *
     * Deliberately POD-ish and free of pointers to the world: it is owned by
     * the AI object, which is owned by the creature, so anything it held would
     * have the creature's lifetime anyway -- and a guid is what survives the
     * creature dying mid-sequence.
     */
    struct Actor
    {
        Phases phases;

        /// What this creature remembers. Named in the tables and numbered
        /// here: the names are interned per creature ENTRY when its rules load,
        /// so a guard costs an array index at run time and still reads as
        /// `enraged=0` where a person looks at it.
        ///
        /// Cleared by Reset, which is what makes "already enraged" mean
        /// already enraged IN THIS FIGHT.
        uint32 states[MaxStates] = {};

        /// Health this creature refuses to drop below, and whether the number
        /// is a percentage. Zero means it dies like anything else.
        uint32 invincibilityHp = 0;
        bool   invincibilityIsPercent = false;

        /// Which AI events this creature will pass on. EventAI wrote it as a
        /// mask and so does this.
        uint32 throwMask = 0;

        /// Whether the AI drives movement and melee at all. A scripted
        /// encounter turns these off while it choreographs something and back
        /// on afterwards, and forgetting the second half is the commonest way
        /// a boss ends up standing still for ever.
        bool combatMovement = true;
        bool meleeAllowed = true;

        void Reset()
        {
            phases = Phases();
            for (uint32& state : states)
            {
                state = 0;
            }
            invincibilityHp = 0;
            invincibilityIsPercent = false;
            throwMask = 0;
            combatMovement = true;
            meleeAllowed = true;
        }
    };

    /**
     * Everything one action needs, gathered once.
     *
     * Passed by reference to every verb, so a verb's signature says exactly
     * what a verb may touch -- and adding something to that list is a visible
     * change rather than a new global reached for quietly.
     */
    struct Doing
    {
        Map*         map = nullptr;
        WorldObject* source = nullptr;   ///< who is doing it
        WorldObject* target = nullptr;   ///< who it is done to
        ObjectGuid   owner;              ///< the player holding an item source

        /// Present when the source is a creature MAI is driving; absent when
        /// the step came from a sequence started by the world, which has no
        /// creature behind it at all.
        Actor*       actor = nullptr;

        /// Set by a verb whose effect was refused rather than done. Only the
        /// casts set it, and only a rule with a retry reads it.
        bool*        refused = nullptr;

        Unit*     SourceUnit() const;
        Creature* SourceCreature() const;
        Unit*     TargetUnit() const;
    };
}

#endif //MANGOS_MAI_ACTOR_H
