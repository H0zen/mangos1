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
 * What one action is given, and what only the AI can carry out.
 *
 * `Doing` is a verb's whole world: passed by reference to every one of them,
 * so a verb's signature says exactly what a verb may touch. `Driver` is the
 * short list of things a free function cannot do at all, because only the AI
 * object can.
 *
 * PHASES AND ACTOR ARE IN MaiScript.h. They were here, beside the verbs that
 * write them, until a FRAME had to be able to carry one: a creature that dies
 * hands the rest of its sequence to the map with its state copied into it (see
 * Frame::actor), and the model header is where the things a frame is made of
 * have to live. Nothing about who writes them changed.
 */
namespace mai
{
    struct Driver;

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

        /// The AI driving this creature, when a creature is running the step.
        /// Null for a sequence the world started, which has no AI behind it.
        Driver*      driver = nullptr;

        Unit*     SourceUnit() const;
        Creature* SourceCreature() const;
        Unit*     TargetUnit() const;
    };

    /**
     * The creature's AI, as much of it as a verb is allowed to touch.
     *
     * An interface rather than a pointer to the AI, because a verb must not
     * know what a creature's AI is -- MaiPerform is testable with no world at
     * all, and that is worth keeping.
     *
     * It was called RuleTimers while arming was the only thing on it, and the
     * other three were written the only way a free function could write them:
     * on to the Actor, where nothing reads them, or through the MotionMaster,
     * behind the AI's back. Both are how a verb ends up half-done -- the
     * creature is told to stand still and the AI, which was never told,
     * chases again at the next retarget. What is here is the whole of what
     * only the AI object can carry out.
     */
    struct Driver
    {
        virtual ~Driver() = default;

        /**
         * Set rule @a id's timer to @a ms, and enable or disable it.
         *
         * A rule arms itself and re-arms itself, which is the whole of what a
         * timer needed to be until a script wanted to say "and cancel that".
         * The shape is always the same: something is given a deadline,
         * something else may make the deadline moot, and whichever happens
         * first must stop the other.
         *
         * Taerar is the example that forced it. He banishes himself and
         * summons three shades; sixty seconds later he comes back, UNLESS the
         * shades die first, in which case he comes back at once. Encoded as a
         * delayed step the sixty-second half survives the shades' death and
         * unbanishes the NEXT banish early; encoded as a guarded rule its
         * timer freezes half-counted rather than resetting. Neither is what
         * the C++ does, and both are wrong by twenty seconds in a fight that
         * lasts three minutes.
         *
         * A rule this creature does not have is ignored: a script naming one
         * is a mistake worth a log, not worth a crash.
         */
        virtual void Arm(uint32 id, uint32 ms, bool enable) = 0;

        /// Whether the AI drives movement in combat at all, told to the AI
        /// and not merely remembered beside it: the flag the base class keeps
        /// is what every later retarget reads.
        /// @param sendMelee  also tell the client the swing is starting or
        ///                   stopping, which is EventAI's own second column.
        virtual void SetCombatMovementAllowed(bool enable, bool sendMelee) = 0;

        /// How this creature chases from here on -- the distance a caster
        /// keeps and the angle it keeps it at. Written to the AI's own pair,
        /// so the NEXT AttackStart chases the same way rather than closing to
        /// nought.
        virtual void SetChase(float distance, float angle) = 0;

        /// Start sequence @a kind / @a id on this creature, keeping the actor,
        /// the selectors and the timers the rule had. @a source and @a target
        /// are whom the branch's steps act as and on, which is whom the step
        /// that started it had after its own flags moved things.
        /// @return false when there is no such sequence, which is not an
        ///         error: a branch that has not been written yet is a branch
        ///         not taken.
        virtual bool StartBranch(uint32 kind, uint32 id, ObjectGuid source,
                                 ObjectGuid target) = 0;
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
        KindItemUse    = 13,

        /// A player stepping into an area trigger, keyed by trigger id. Runs
        /// INLINE for the same reason `item_use` does: the seam asks whether
        /// anything handled it, and a queued sequence has no answer yet.
        KindAreaTrigger = 14
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
    /// @param cancel  non-null when the CALLER is running inline, in which
    ///                case the branch runs inline too and its answer comes
    ///                back through here. A branch of an inline sequence that
    ///                queued itself could not refuse anything, because the
    ///                thing it was refusing would have happened by the time it
    ///                ran.
    bool StartSequence(Map* map, uint32 kind, uint32 id, WorldObject* source,
                       WorldObject* target, ObjectGuid owner, ObjectGuid item,
                       bool* cancel);

    /**
     * The sequence @a kind / @a id names, or nullptr when there is none.
     *
     * Declared here and defined by the engine for the same reason
     * StartSequence is: a creature's AI runs a branch on ITSELF -- that is
     * what keeps the actor, the phase and the selectors the rule had -- and to
     * do that it needs the steps, not a request to somebody else to run them.
     *
     * The result is owned by the engine, and it does NOT outlive a reload.
     * `.reload mai_script` frees every sequence in the table; the engine drops
     * its own frames, and it has no way to reach a creature that started a
     * branch and is holding one of these. Anything that keeps the pointer past
     * the call must keep SequenceStamp() beside it -- see Frame::stamp.
     */
    Sequence const* FindSequence(uint32 kind, uint32 id);

    /**
     * Which loading of the shared sequence table is current.
     *
     * Bumped every time the table is rebuilt, and never zero, so zero is free
     * to mean "this frame does not point into the table at all". Declared here
     * and defined by the engine, like the two above, so that nothing which
     * merely RUNS a frame has to include the engine to ask.
     */
    uint32 SequenceStamp();

    /**
     * Hand a half-run frame to the map, which ticks whatever happens to the
     * creature that started it.
     *
     * What a death rule needs, and the reason a death rule could not wait. The
     * AI object stops being ticked the moment its creature is not alive --
     * `Creature::Update` reaches `UpdateAI` only on an ALIVE creature that is
     * not walking home -- so a step with a time on it had nobody to run it and
     * the Reset that follows death threw it away.
     *
     * The frame carries the creature's state with it (Frame::actor), because
     * the map has no creature to ask. What it cannot carry is the AI object
     * itself, so the verbs that reach one -- `set_timer`, and the two that
     * drive movement -- have nothing to reach: they are no-ops on an adopted
     * frame, which is what they mean anyway once the creature is a corpse.
     *
     * Safe to call from the creature's own map thread, which is the only
     * caller: it appends to that map's frames, and the map's own walk is
     * indexed and copies, so an append during it is ordinary.
     */
    void AdoptFrame(Map* map, Frame const& frame);
}

#endif //MANGOS_MAI_ACTOR_H
