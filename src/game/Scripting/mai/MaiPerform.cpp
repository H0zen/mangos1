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
            case ActionId::SetPhase:          return SetPhase(doing, step);
            case ActionId::IncPhase:          return IncPhase(doing, step);
            case ActionId::RandomPhase:       return RandomPhase(doing, step);

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
            case ActionId::SetThrowMask:      return SetThrowMask(doing, step);
            case ActionId::ThrowAiEvent:      return ThrowAiEvent(doing, step);

            case ActionId::SetInstanceData:   return SetInstanceData(doing, step);
            case ActionId::SetInstanceData64: return SetInstanceData64(doing, step);

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
