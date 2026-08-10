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

// Doing what a step says.
//
// WHAT IS HERE AND WHAT IS NOT, and the line between them is deliberate.
//
// The 26 verbs EventAI had and the DB scripts did not -- phases, threat,
// instance data, the movement switches -- are implemented here, natively.
// They have to be: there is no old body to borrow, EventAI's implementation of
// them being a switch inside its interpreter rather than anything callable.
//
// The 47 the DB scripts had still go to ScriptAction, and will until there is
// something that would catch a mistake in rewriting them. The differential
// test compares TIMING -- same step, same order, same tick -- and would not
// notice a rewritten body casting the wrong spell. Rewriting forty-seven
// effects with no check on their effects is how a migration acquires a bug
// nobody can date.
//
// A verb returns true when the sequence should stop there.

#include "MaiPerform.h"

#include "MaiTargeting.h"

#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceData.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Unit.h"

namespace mai
{
    Unit* Doing::SourceUnit() const
    {
        return source ? source->ToUnit() : nullptr;
    }

    Creature* Doing::SourceCreature() const
    {
        return source ? source->ToCreature() : nullptr;
    }

    Unit* Doing::TargetUnit() const
    {
        return target ? target->ToUnit() : nullptr;
    }

    namespace
    {
        /// An operand the step gave, or @a fallback when it did not. The
        /// distinction matters: `inc_phase by=0` and `inc_phase` are not the
        /// same instruction, and only one of them is a mistake.
        uint32 Given(Step const& step, std::size_t slot, uint32 fallback = 0)
        {
            return step.Has(slot) ? step.operands[slot].u : fallback;
        }

        float GivenF(Step const& step, std::size_t slot, float fallback = 0.0f)
        {
            return step.Has(slot) ? step.operands[slot].f : fallback;
        }

        // ---- phases ---------------------------------------------------------

        bool SetPhase(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->phases.current = Given(step, 0) & 31u;
            }
            return false;
        }

        bool IncPhase(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            // Signed, and clamped rather than wrapped. EventAI let a phase
            // walk off either end and the result was a creature in a phase no
            // rule mentions -- which looks exactly like a creature that has
            // stopped working, and is impossible to tell from one.
            int32 const by = step.Has(0) ? step.operands[0].i : 1;
            int32 const now = int32(doing.actor->phases.current) + by;
            doing.actor->phases.current = uint32(now < 0 ? 0
                                                         : (now > 31 ? 31 : now));
            return false;
        }

        bool RandomPhase(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            uint32 choices[3];
            uint32 count = 0;
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (step.Has(slot))
                {
                    choices[count++] = step.operands[slot].u;
                }
            }

            if (count)
            {
                doing.actor->phases.current = choices[urand(0, count - 1)] & 31u;
            }
            return false;
        }

        bool RandomPhaseRange(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            uint32 const least = Given(step, 0);
            uint32 const most = Given(step, 1);

            // An inverted range is the row's mistake, not a reason to pick
            // one end of it silently: the original swapped them, and so does
            // this, because a phase is still a phase either way round.
            uint32 const low = least < most ? least : most;
            uint32 const high = least < most ? most : least;

            doing.actor->phases.current = urand(low, high) & 31u;
            return false;
        }

        // ---- one of three ---------------------------------------------------

        /// The slots that were actually given, which is what "one of three"
        /// has to choose between. A row with two sounds must not pick the
        /// third an eighth of the time and play silence.
        uint32 OneOfThree(Step const& step, bool& any)
        {
            uint32 choices[3];
            uint32 count = 0;
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (step.Has(slot))
                {
                    choices[count++] = step.operands[slot].u;
                }
            }

            any = count != 0;
            return any ? choices[urand(0, count - 1)] : 0;
        }

        bool RandomSound(Doing& doing, Step const& step)
        {
            bool any = false;
            uint32 const sound = OneOfThree(step, any);

            if (any && doing.source)
            {
                doing.source->PlayDirectSound(sound);
            }
            return false;
        }

        bool RandomEmote(Doing& doing, Step const& step)
        {
            bool any = false;
            uint32 const emote = OneOfThree(step, any);

            if (any)
            {
                if (Unit* self = doing.SourceUnit())
                {
                    self->HandleEmote(emote);
                }
            }
            return false;
        }

        /**
         * Casting, the way an AI casts.
         *
         * THE FIRST BORROWED BODY TO BE REPLACED, and it earns it: 10,512 of
         * the 27,561 converted steps are this verb, and every one of them came
         * from an EventAI row that called DoCastSpellIfCan. The DB-script body
         * they were falling through to calls Unit::CastSpell outright, and the
         * two differ in ways that show up as a boss behaving oddly rather than
         * as anything failing:
         *
         *   * DoCastSpellIfCan REFUSES while the creature is already casting
         *     something non-triggered. The raw call does not, so a creature
         *     with three timers coming due in one scan tries three casts and
         *     interrupts itself twice.
         *   * it runs CanCastSpell first -- range, line of sight, silence,
         *     immunity -- and the raw call leaves all of that to the spell
         *     system, which fails later and differently.
         *   * it needs no target to refuse gracefully; the borrowed body logs
         *     a database error every time one is missing.
         *
         * The flags operand is the CAST_* vocabulary, unchanged, because that
         * is what EventAI's own column held. `command_additional` on the step
         * means triggered, which is how the DB scripts spelled the same thing.
         */
        bool CastSpell(Doing& doing, Step const& step, bool& handled)
        {
            Creature* self = doing.SourceCreature();
            Unit* victim = doing.TargetUnit();

            // Only a creature with an AI casts this way. A sequence the world
            // started may have a game object or a player as its source, and
            // neither has a creature AI to ask -- so those fall through to the
            // borrowed body exactly as before.
            if (!self || !self->AI())
            {
                handled = false;
                return false;
            }

            uint32 flags = Given(step, 1);
            if (step.buddy.flags & CommandAdditional)
            {
                flags |= CAST_TRIGGERED;
            }

            self->AI()->DoCastSpellIfCan(victim ? victim : self,
                                         Given(step, 0), flags);
            return false;
        }

        /**
         * Arrive at the target, instantly.
         *
         * A near-teleport rather than a movement: the target's own position,
         * the caster's own facing. That is what Shazzrah's Gate does, and what
         * the ScriptDev script wrote by hand under a comment reading
         * TODO REMOVE HACK -- because the spell's dummy effect had nowhere to
         * live. It has somewhere now: the effect reaches MAI as a sequence
         * keyed on the spell, and this is the one verb that sequence needed.
         */
        bool TeleportToTarget(Doing& doing, Step const& step)
        {
            (void)step;

            Unit* self = doing.SourceUnit();
            Unit* victim = doing.TargetUnit();

            if (!self || !victim || self == victim)
            {
                sLog.outErrorDb("MAI: teleport_to_target needs a source and a "
                                "different target");
                return false;
            }

            self->NearTeleportTo(victim->Where().X(), victim->Where().Y(),
                                 victim->Where().Z(), self->Where().Facing());
            return false;
        }

        bool SetHealth(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            if (!self || !self->IsAlive())
            {
                return false;
            }

            // Out of 100 and clamped there. The maximum is the creature's own,
            // so a script says "to full" and stays right when the template
            // changes underneath it.
            uint32 const percent = Given(step, 0) > 100 ? 100 : Given(step, 0);
            uint32 const wanted = (self->GetMaxHealth() * percent) / 100;

            self->SetHealth(wanted ? wanted : 1);
            return false;
        }

        // ---- what a creature remembers --------------------------------------

        bool SetState(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            std::size_t const slot = Given(step, 0);
            if (slot < MaxStates)
            {
                doing.actor->states[slot] = Given(step, 1);
            }
            return false;
        }

        bool AddState(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            std::size_t const slot = Given(step, 0);
            if (slot >= MaxStates)
            {
                return false;
            }

            // Signed, and floored at zero rather than wrapped. A count that
            // goes below zero becomes four billion, and a guard reading
            // `kills>=3` would then be true for ever.
            int32 const by = step.Has(1) ? step.operands[1].i : 1;
            int64 const now = int64(doing.actor->states[slot]) + by;
            doing.actor->states[slot] = uint32(now < 0 ? 0 : now);
            return false;
        }

        // ---- threat ---------------------------------------------------------

        bool ThreatChange(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            if (!self)
            {
                return false;
            }

            int32 const percent = step.Has(0) ? step.operands[0].i : 0;
            bool const all = Given(step, 1) != 0;

            ThreatList const& threats = self->GetThreatManager().getThreatList();
            for (HostileReference* reference : threats)
            {
                Unit* victim = reference->getTarget();
                if (!victim)
                {
                    continue;
                }
                if (!all && victim != doing.target)
                {
                    continue;
                }

                float const held = self->GetThreatManager()
                                       .getThreat(victim);
                self->GetThreatManager()
                    .modifyThreatPercent(victim, percent);
                (void)held;
            }
            return false;
        }

        bool CallForHelp(Doing& doing, Step const& step)
        {
            if (Creature* self = doing.SourceCreature())
            {
                self->CallForHelp(GivenF(step, 0, 5.0f));
            }
            return false;
        }

        bool FleeForAssist(Doing& doing, Step const&)
        {
            if (Creature* self = doing.SourceCreature())
            {
                self->DoFleeToGetAssistance();
            }
            return false;
        }

        bool ZoneCombatPulse(Doing& doing, Step const&)
        {
            if (Creature* self = doing.SourceCreature())
            {
                self->SetInCombatWithZone();
            }
            return false;
        }

        // ---- the AI's own switches ------------------------------------------

        bool AutoAttack(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->meleeAllowed = Given(step, 0) != 0;
            }
            return false;
        }

        bool CombatMovement(Doing& doing, Step const& step)
        {
            if (!doing.actor)
            {
                return false;
            }

            doing.actor->combatMovement = Given(step, 0) != 0;

            // The second parameter says whether to start chasing right now
            // rather than at the next decision. Without it a creature told to
            // resume movement stands still until something else moves it,
            // which reads as the script having failed.
            if (Creature* self = doing.SourceCreature())
            {
                if (doing.actor->combatMovement && Given(step, 1) != 0)
                {
                    if (Unit* victim = self->getVictim())
                    {
                        self->GetMotionMaster()->MoveChase(victim);
                    }
                }
                else if (!doing.actor->combatMovement)
                {
                    self->GetMotionMaster()->MoveIdle();
                }
            }
            return false;
        }

        bool RangedMovement(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            if (!self)
            {
                return false;
            }

            if (Unit* victim = self->getVictim())
            {
                self->GetMotionMaster()->MoveChase(victim, GivenF(step, 0),
                                                   GivenF(step, 1));
            }
            return false;
        }

        bool ChangeMovement(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            if (!self)
            {
                return false;
            }

            switch (Given(step, 0))
            {
                case IDLE_MOTION_TYPE:
                    self->GetMotionMaster()->MoveIdle();
                    break;
                case RANDOM_MOTION_TYPE:
                {
                    Geometry::Placement const& where = self->Where();
                    self->GetMotionMaster()->MoveRandomAroundPoint(
                        where.X(), where.Y(), where.Z(), GivenF(step, 1, 5.0f));
                    break;
                }
                case WAYPOINT_MOTION_TYPE:
                    self->GetMotionMaster()->MoveWaypoint();
                    break;
                default:
                    sLog.outErrorDb("MAI: change_movement %u is not a movement "
                                    "type", Given(step, 0));
                    break;
            }
            return false;
        }

        bool Evade(Doing& doing, Step const&)
        {
            if (Creature* self = doing.SourceCreature())
            {
                if (self->AI())
                {
                    self->AI()->EnterEvadeMode();
                }
            }
            return false;
        }

        bool Die(Doing& doing, Step const&)
        {
            Unit* self = doing.SourceUnit();
            if (self && self->IsAlive())
            {
                self->DealDamage(self, self->GetHealth(), nullptr,
                                 DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL,
                                 nullptr, false);
            }
            return false;
        }

        bool SetInvincibility(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->invincibilityHp = Given(step, 0);
                doing.actor->invincibilityIsPercent = Given(step, 1) != 0;
            }
            return false;
        }

        bool SetThrowMask(Doing& doing, Step const& step)
        {
            if (doing.actor)
            {
                doing.actor->throwMask = Given(step, 0);
            }
            return false;
        }

        bool ThrowAiEvent(Doing& doing, Step const& step)
        {
            Creature* self = doing.SourceCreature();
            if (!self)
            {
                return false;
            }

            self->AI() && (self->AI()->SendAIEventAround(
                               AIEventType(Given(step, 0)),
                               doing.TargetUnit(), 0,
                               GivenF(step, 1, 10.0f)), true);
            return false;
        }

        // ---- instance state -------------------------------------------------

        bool SetInstanceData(Doing& doing, Step const& step)
        {
            if (!doing.map)
            {
                return false;
            }

            if (InstanceData* data = doing.map->GetInstanceData())
            {
                data->SetData(Given(step, 0), Given(step, 1));
            }
            else
            {
                sLog.outErrorDb("MAI: set_instance_data on map %u, which has "
                                "no instance data", doing.map->GetId());
            }
            return false;
        }

        bool SetInstanceData64(Doing& doing, Step const& step)
        {
            if (!doing.map)
            {
                return false;
            }

            if (InstanceData* data = doing.map->GetInstanceData())
            {
                // Two halves, because an operand is one 32-bit cell. The high
                // one is optional and absent means zero, which is what a table
                // that only ever stored a guid low part wants.
                uint64 const value = uint64(Given(step, 1)) |
                                     (uint64(Given(step, 2)) << 32);
                data->SetData64(Given(step, 0), value);
            }
            return false;
        }

        bool SetInstanceDataGuid(Doing& doing, Step const& step)
        {
            if (!doing.map)
            {
                return false;
            }

            // WHO, not a number. The target is whoever the step's selector
            // picked, and its guid is the value -- which is why this cannot be
            // set_instance_data64 with two literal halves: nothing knows the
            // number until the step runs.
            if (!doing.target)
            {
                sLog.outErrorDb("MAI: set_instance_data_guid with no target");
                return false;
            }

            if (InstanceData* data = doing.map->GetInstanceData())
            {
                data->SetData64(Given(step, 0),
                                doing.target->GetObjectGuid().GetRawValue());
            }
            else
            {
                sLog.outErrorDb("MAI: set_instance_data_guid on map %u, which "
                                "has no instance data", doing.map->GetId());
            }
            return false;
        }

        // ---- fields and flags -----------------------------------------------

        bool SetUnitField(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            uint32 const field = Given(step, 0);

            // Bounded by the object's own block. Unbounded, this is the one
            // verb in the set that can stop the server from a table: every
            // accessor under it asserts, and MANGOS_ASSERT is live in release.
            if (!self || field >= self->GetValuesCount())
            {
                sLog.outErrorDb("MAI: set_unit_field %u is past the end of the "
                                "object's block", field);
                return false;
            }

            self->SetUInt32Value(field, Given(step, 1));
            return false;
        }

        bool SetUnitFlag(Doing& doing, Step const& step)
        {
            if (Unit* self = doing.SourceUnit())
            {
                self->SetFlag(UNIT_FIELD_FLAGS, Given(step, 0));
            }
            return false;
        }

        bool RemoveUnitFlag(Doing& doing, Step const& step)
        {
            if (Unit* self = doing.SourceUnit())
            {
                self->RemoveFlag(UNIT_FIELD_FLAGS, Given(step, 0));
            }
            return false;
        }

        bool SetSheath(Doing& doing, Step const& step)
        {
            if (Unit* self = doing.SourceUnit())
            {
                uint32 const state = Given(step, 0);
                if (state < MAX_SHEATH_STATE)
                {
                    self->SetSheath(SheathState(state));
                }
            }
            return false;
        }

        bool EmoteTarget(Doing& doing, Step const& step)
        {
            Unit* self = doing.SourceUnit();
            Unit* victim = doing.TargetUnit();
            if (self && victim)
            {
                self->HandleEmote(Given(step, 0));
            }
            return false;
        }

        // ---- quests ---------------------------------------------------------

        bool QuestEvent(Doing& doing, Step const& step)
        {
            uint32 const quest = Given(step, 0);
            bool const all = Given(step, 1) != 0;

            if (Player* player = doing.target ? doing.target->ToPlayer()
                                              : nullptr)
            {
                if (all && player->GetGroup())
                {
                    // The whole group gets it, which is what the flag is for
                    // and what a single-player credit quietly failed to do.
                    Group* group = player->GetGroup();
                    for (GroupReference* itr = group->GetFirstMember(); itr;
                         itr = itr->next())
                    {
                        if (Player* member = itr->getSource())
                        {
                            member->AreaExploredOrEventHappens(quest);
                        }
                    }
                }
                else
                {
                    player->AreaExploredOrEventHappens(quest);
                }
            }
            return false;
        }

        bool KilledMonster(Doing& doing, Step const& step)
        {
            if (Player* player = doing.target ? doing.target->ToPlayer()
                                              : nullptr)
            {
                player->KilledMonsterCredit(Given(step, 0),
                                            doing.source
                                                ? doing.source->GetObjectGuid()
                                                : ObjectGuid());
            }
            return false;
        }

        bool CastEvent(Doing& doing, Step const& step)
        {
            if (Player* player = doing.target ? doing.target->ToPlayer()
                                              : nullptr)
            {
                player->CastedCreatureOrGO(Given(step, 0),
                                           doing.source
                                               ? doing.source->GetObjectGuid()
                                               : ObjectGuid(),
                                           Given(step, 1));
            }
            return false;
        }
    }

    bool PerformNative(Doing& doing, Step const& step, bool& handled)
    {
        handled = true;

        switch (step.action)
        {
            case ActionId::CastSpell:         return CastSpell(doing, step,
                                                                handled);

            case ActionId::SetState:          return SetState(doing, step);
            case ActionId::AddState:          return AddState(doing, step);

            case ActionId::SetPhase:          return SetPhase(doing, step);
            case ActionId::IncPhase:          return IncPhase(doing, step);
            case ActionId::RandomPhase:       return RandomPhase(doing, step);
            case ActionId::RandomPhaseRange:  return RandomPhaseRange(doing, step);
            case ActionId::RandomSound:       return RandomSound(doing, step);
            case ActionId::RandomEmote:       return RandomEmote(doing, step);

            case ActionId::ThreatChange:      return ThreatChange(doing, step);
            case ActionId::CallForHelp:       return CallForHelp(doing, step);
            case ActionId::FleeForAssist:     return FleeForAssist(doing, step);
            case ActionId::ZoneCombatPulse:   return ZoneCombatPulse(doing, step);

            case ActionId::AutoAttack:        return AutoAttack(doing, step);
            case ActionId::CombatMovement:    return CombatMovement(doing, step);
            case ActionId::RangedMovement:    return RangedMovement(doing, step);
            case ActionId::ChangeMovement:    return ChangeMovement(doing, step);
            case ActionId::Evade:             return Evade(doing, step);
            case ActionId::Die:               return Die(doing, step);
            case ActionId::SetInvincibility:  return SetInvincibility(doing, step);
            case ActionId::SetHealth:         return SetHealth(doing, step);
            case ActionId::TeleportToTarget:  return TeleportToTarget(doing, step);
            case ActionId::SetThrowMask:      return SetThrowMask(doing, step);
            case ActionId::ThrowAiEvent:      return ThrowAiEvent(doing, step);

            case ActionId::SetInstanceData:   return SetInstanceData(doing, step);
            case ActionId::SetInstanceData64: return SetInstanceData64(doing, step);
            case ActionId::SetInstanceDataGuid:
                                              return SetInstanceDataGuid(doing, step);

            case ActionId::SetUnitField:      return SetUnitField(doing, step);
            case ActionId::SetUnitFlag:       return SetUnitFlag(doing, step);
            case ActionId::RemoveUnitFlag:    return RemoveUnitFlag(doing, step);
            case ActionId::SetSheath:         return SetSheath(doing, step);
            case ActionId::EmoteTarget:       return EmoteTarget(doing, step);

            case ActionId::QuestEvent:        return QuestEvent(doing, step);
            case ActionId::KilledMonster:     return KilledMonster(doing, step);
            case ActionId::CastEvent:         return CastEvent(doing, step);

            default:
                // One of the 47 the DB scripts already implement. Not an
                // error: see the note at the top of this file for why they are
                // still borrowed rather than rewritten.
                handled = false;
                return false;
        }
    }
}
