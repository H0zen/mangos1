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

#ifndef MANGOS_MAI_CREATURE_AI_H
#define MANGOS_MAI_CREATURE_AI_H

#include "MaiActor.h"
#include "MaiRule.h"
#include "MaiScript.h"

#include "CreatureAI.h"

#include <cstddef>
#include <vector>

/**
 * One creature, driven by rules.
 *
 * WHAT THIS IS NOT is the shortest way to say what it is. CreatureEventAI is
 * 2,200 lines that are four things at once: the trigger table, the timer
 * wheel, the interpreter and all fifty action bodies. Every one of those is
 * reachable only through a live creature on a live map, which is why the
 * system has never had a test.
 *
 * Here they are four things. The action bodies are MaiPerform's, shared with
 * the sequences the world starts. Advancing through time is MaiRunner's, and
 * is tested with no server at all. Deciding who a step acts on is
 * MaiTargeting's and MaiSelect's. What is left -- and it is all that is left
 * -- is this: WHEN does a rule fire.
 *
 * That question has three parts and they are genuinely intertwined, which is
 * why they stayed together:
 *
 *   * a trigger, which is a callback the world already makes
 *   * a timer, which re-arms itself and must not be decremented in a phase the
 *     rule cannot fire in
 *   * a chance, rolled once per firing and SHARED by the steps that follow, so
 *     that a rule with three actions does not roll three times
 *
 * The rules themselves are shared, per creature ENTRY, and live in the engine.
 * What is here is one creature's worth of state: which rules are still armed,
 * what their timers stand at, the phase, and the sequences currently running.
 */
namespace mai
{
    class MaiCreatureAI : public CreatureAI, public Driver
    {
        public:
            /// @a rules may be null: an entry bound to MAI with no rows is not
            /// an error, and drives the creature with no rules at all.
            MaiCreatureAI(Creature* creature, RuleSet const* rules);

            void GetAIInformation(ChatHandler& reader) override;

            // -- the triggers. Each is a callback the world already made; all
            //    any of them does is name a RuleId and hand over what it knew.

            void JustRespawned() override;
            void Reset() override;
            void JustReachedHome() override;
            void EnterCombat(Unit* enemy) override;
            void EnterEvadeMode() override;
            void JustDied(Unit* killer) override;
            void KilledUnit(Unit* victim) override;
            void JustSummoned(Creature* summoned) override;
            void SummonedCreatureJustDied(Creature* summoned) override;
            void SummonedCreatureDespawn(Creature* summoned) override;
            void AttackStart(Unit* who) override;
            void MoveInLineOfSight(Unit* who) override;
            void SpellHit(Unit* caster, SpellEntry const* spell) override;
            void SpellHitTarget(Unit* victim,
                                SpellEntry const* spell) override;
            void DamageTaken(Unit* dealer, uint32& damage) override;
            void HealedBy(Unit* healer, uint32& healed) override;
            void ReceiveEmote(Player* player, uint32 emote) override;
            void ReceiveAIEvent(AIEventType type, Creature* sender,
                                Unit* invoker, uint32 misc) override;
            void MovementInform(uint32 type, uint32 pointId) override;

            void UpdateAI(uint32 diff) override;

            /// Driver: set one of this creature's rules to fire in @a ms,
            /// or stop it firing at all. What `set_timer` reaches.
            void Arm(uint32 id, uint32 ms, bool enable) override;

            /// Driver: the three a verb cannot carry out on its own.
            void SetCombatMovementAllowed(bool enable, bool sendMelee) override;
            void SetChase(float distance, float angle) override;
            bool StartBranch(uint32 kind, uint32 id, ObjectGuid source,
                             ObjectGuid target) override;

            bool IsVisible(Unit* who) const override;

            /// A player accepted or handed in a quest to this creature. Not a
            /// CreatureAI callback -- the world has never had one -- so the
            /// engine calls it from the event it does raise.
            void QuestFor(Player* player, uint32 questId, bool accepted);

        private:
            /**
             * One rule, and the two things that differ between two creatures
             * running it.
             *
             * The rule is shared and const; the timer and the enabled bit are
             * this creature's own. EventAI copied the whole event -- all
             * fifteen fields and its three actions -- per creature per event,
             * which for a zone full of a hundred copies of one entry is a
             * hundred copies of a table that never changes.
             */
            struct Armed
            {
                Rule const* rule = nullptr;
                uint32      timeMs = 0;     ///< until it may fire; 0 is now
                bool        enabled = true;
            };

            /// Whether this rule may fire at all right now: armed, not waiting,
            /// not in a phase it is excluded from, and past its guards.
            bool Ready(Armed const& armed) const;

            /// Whether every guard holds. Separate from Ready because the
            /// TIMER asks it too -- a guarded rule's clock is frozen while the
            /// guard is false, exactly as a phase freezes it.
            bool Allowed(Rule const& rule) const;

            /// Set the timer from a pair of operand slots holding min and max.
            /// @return false when the pair is inverted, which DISABLES the
            ///         rule -- the original's answer, and a loud one.
            bool ReArm(Armed& armed, std::size_t minSlot, std::size_t maxSlot);

            /**
             * Every rule with this trigger, in table order.
             *
             * @a now runs the steps that are already due rather than leaving
             * them for the next UpdateAI, and is for the three callbacks after
             * which there is NO next UpdateAI: a corpse is not ALIVE and a
             * creature walking home is in evade mode, so Creature::Update
             * skips both. EventAI ran every action synchronously in the
             * callback and never had the question.
             */
            void Fire(RuleId trigger, Unit* invoker = nullptr,
                      Creature* sender = nullptr, bool now = false);

            /// One rule: the phase, the trigger's own condition, the chance,
            /// and then the sequence.
            /// @return true when it fired.
            bool Fire(Armed& armed, Unit* invoker = nullptr,
                      Creature* sender = nullptr, bool now = false);

            /// Whether the trigger's own condition holds, and re-arm it if so.
            /// Split from Fire because this is the half that reads the world.
            bool Holds(Armed& armed, Unit*& invoker);

            /// Start this rule's steps. They may not all be at time zero --
            /// EventAI's always were, but a rule is not required to be an
            /// EventAI row.
            void Start(Rule const& rule, Unit* invoker, Creature* sender,
                       bool now = false);

            /// Advance every running sequence by @a diff.
            void Advance(uint32 diff);

            /**
             * One frame, by INDEX, advanced by @a diff.
             *
             * By index and not by reference, and that is the whole of what
             * keeps this safe: a step can start another sequence on this
             * creature, which appends to m_frames and may reallocate it, and a
             * step can END the creature -- `die` reaches JustDied, which
             * cancels every frame there is. Either one leaves a `Frame&` taken
             * before the call pointing at memory that is no longer the frame.
             */
            void RunFrame(std::size_t index, uint32 diff);

            /**
             * Drop everything still running, safely from inside a step.
             *
             * Reset() cannot simply clear the vector: it is reached from
             * JustDied, which is reached from a `die` STEP, which the loop in
             * RunFrame is in the middle of walking. So the frames are marked
             * finished instead and swept when the walk is over -- which is
             * also what lets the death rules queued immediately afterwards
             * survive, since they are appended after the marking.
             */
            void DropFrames();

            /// Erase the frames that have finished. Never while a step is
            /// running: erasing is the one thing that moves the frames an
            /// index points at.
            void Sweep();

            /// A refused cast asks its rule to come round again sooner, when
            /// the rule said how soon.
            void Retry(Frame const& frame);

            /// The rules whose timers tick rather than waiting for a callback.
            void Tick(uint32 diff);

            Actor              m_actor;
            std::vector<Armed> m_armed;
            std::vector<Frame> m_frames;

            /// Events are looked at every EVENT_UPDATE_TIME rather than every
            /// tick, and the leftover is carried rather than dropped. EventAI's
            /// own scheme, kept: a boss with forty rules would otherwise walk
            /// them at the map's full rate for no gain.
            uint32 m_untilLookMs = 0;
            uint32 m_sinceLookMs = 0;

            /// Which health marks this creature has already announced. 100 is
            /// "done", which is the original's sentinel and not a percentage.
            uint32 m_throwStep = 0;

            /// How many RunFrame calls are on the stack. A step may start a
            /// branch, which runs a frame from inside a frame, and the sweep
            /// that erases finished frames must not run until the outermost
            /// one is done: erasing is the one thing that moves the frames an
            /// index points at.
            uint32 m_running = 0;

            /// Whether any rule waits on line of sight, cached because
            /// MoveInLineOfSight is the hottest callback in the server.
            bool m_watchesSight = false;
    };
}

#endif //MANGOS_MAI_CREATURE_AI_H
