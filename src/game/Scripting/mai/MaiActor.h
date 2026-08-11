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

        /// Whoever this creature was told to remember, for the steps that come
        /// after the one that chose. A guid rather than a pointer for the
        /// reason everything here is: the fight outlives the choosing, and
        /// whoever was picked can die between two beats of it.
        ///
        /// Cleared by Reset, so a focus does not survive a wipe.
        ObjectGuid remembered;

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
            remembered.Clear();
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

    struct RuleTimers;

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

        /// The creature whose RULE this is, before the four flags moved
        /// anything. `source` is who ends up acting -- often a buddy or a
        /// summon -- and this is who it is acting for.
        ///
        /// Only one verb reads it, and it is the reason it exists: a spell can
        /// be cast BY one unit and credited TO another, which is how a poison
        /// cloud's damage is attributed to the boss that made the cloud.
        WorldObject* ruleOwner = nullptr;

        /// Set by a verb whose effect was refused rather than done. Only the
        /// casts set it, and only a rule with a retry reads it.
        bool*        refused = nullptr;

        /// The item whose use started this, when one did. A guid rather than
        /// a pointer for the reason everything here is one -- and the player
        /// in `owner` is who to ask for it.
        ObjectGuid   item;

        /// Set by a verb that REFUSES the thing that started the sequence.
        /// Only `refuse_use` sets it, and only an inline run reads it: a
        /// queued sequence has nothing left to refuse by the time it runs.
        bool*        cancel = nullptr;

        /// This creature's own rule timers, when a creature is running the
        /// step. Null for a sequence the world started, which has no rules to
        /// arm.
        RuleTimers*  timers = nullptr;

        Unit*     SourceUnit() const;
        Creature* SourceCreature() const;
        Unit*     TargetUnit() const;
    };

    /**
     * Reaching this creature's own rule timers, from inside a step.
     *
     * A rule arms itself and re-arms itself, which is the whole of what a
     * timer needed to be until a script wanted to say "and cancel that". The
     * shape is always the same: something is given a deadline, something else
     * may make the deadline moot, and whichever happens first must stop the
     * other.
     *
     * Taerar is the example that forced it. He banishes himself and summons
     * three shades; sixty seconds later he comes back, UNLESS the shades die
     * first, in which case he comes back at once. Encoded as a delayed step
     * the sixty-second half survives the shades' death and unbanishes the
     * NEXT banish early; encoded as a guarded rule its timer freezes half-
     * counted rather than resetting. Neither is what the C++ does, and both
     * are wrong by twenty seconds in a fight that lasts three minutes.
     *
     * An interface rather than a pointer to the AI, because a verb must not
     * know what a creature's AI is -- MaiPerform is testable with no world at
     * all, and that is worth keeping.
     */
    struct RuleTimers
    {
        virtual ~RuleTimers() = default;

        /// Set rule @a id's timer to @a ms, and enable or disable it. A rule
        /// this creature does not have is ignored: a script naming one is a
        /// mistake worth a log, not worth a crash.
        virtual void Arm(uint32 id, uint32 ms, bool enable) = 0;
    };

    /**
     * The sequence kinds MAI has that `db_scripts` never did.
     *
     * Numbering carries on from DBScriptType's ten, and they are written HERE
     * rather than appended to that enum on purpose: DBScriptType is the DB
     * scripts' own vocabulary, one value per `dbscripts_on_*` table, and these
     * three have no table behind them. Adding them there would have said that
     * `dbscripts_on_aura_apply` exists.
     */
    enum : uint32
    {
        KindAuraApply  = 10,    ///< a dummy aura going on, keyed by spell
        KindAuraRemove = 11,    ///< and coming off

        /// A sequence nothing in the world starts: only `start_script` and
        /// `random_script` do, which is what makes a branch a branch. Its ids
        /// are its own and mean nothing outside MAI.
        KindBranch     = 12,

        /// A player using an item, keyed by item entry, and the only kind that
        /// runs INLINE: its answer is whether the item's own spell may go
        /// ahead, and an answer that arrives on the next map tick is not an
        /// answer. `refuse_use` is what says no.
        KindItemUse    = 13
    };

    /**
     * Start another sequence, on the same map, with these actors.
     *
     * Defined by the engine that owns the sequence table. Declared here
     * because a VERB needs it, and the verbs must not include the engine --
     * that direction is what keeps MaiPerform testable without a world.
     *
     * @return false when there is no such sequence, which is not an error: a
     *         branch that has not been written yet is a branch not taken.
     */
    bool StartSequence(Map* map, uint32 kind, uint32 id, WorldObject* source,
                       WorldObject* target, ObjectGuid owner);
}

#endif //MANGOS_MAI_ACTOR_H
