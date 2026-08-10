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

// EventAI's rows into MAI rules.
//
// The TRIGGER half is generic, exactly as the DB scripts' lowering is, and for
// the same reason: rules.manifest was extracted from the very union that
// defines an EventAI event, so a row's four params are the trigger's four
// parameters in order, and a loop copies them.
//
// The ACTION half is NOT, and that difference is the whole content of this
// file. MaiLowering can be forty generic lines because actions.manifest was
// extracted from `ScriptInfo`'s union -- one union, one order, no mapping. An
// EventAI action is a DIFFERENT union describing the same verbs, and the two
// disagree constantly:
//
//   * `cast` is (spell, TARGET, flags) here and (spell, flags) there
//   * `remove_aura` is (TARGET, spell) here and (spell) there -- reversed
//   * `morph` takes an entry AND a model id here, and one field plus a flag
//     there
//
// A positional loop over three params gets all three of those wrong, and gets
// them wrong SILENTLY. So the mapping is DECLARED, in eventai.map, and this
// file reads the table the generator emits from it. It is declared rather than
// written here because the SQL conversion needs the same fifty rows and is
// written in Python: two copies would drift, and the drift would be as
// invisible as the bug the mapping exists to prevent.
//
// THE SELECTOR IS NOT A PARAMETER. It is the same concept in a different
// column per verb, which is why it comes out of the operands entirely and onto
// the Step, next to the buddy. What remains per verb is one flag: whether the
// creature acts ON the selected unit (`cast`, `quest_event` -- BuddyAsTarget)
// or the selected unit IS the actor (`set_unit_field`, `remove_aura` -- no
// flag, so the buddy replaces the source). That distinction already had a
// mechanism; this only has to say which verbs want which.

#include "MaiRuleLowering.h"

#include "MaiEventAiMap.gen.h"
#include "MaiTargeting.h"

#include "eventai/engine/CreatureEventAI.h"
#include "eventai/engine/CreatureEventAIMgr.h"

#include <cstdio>

namespace mai
{
    namespace
    {
        /// Store @a value in @a slot as the manifest says that slot is typed.
        void Put(Step& step, ActionSpec const& spec, std::size_t slot,
                 uint32 value)
        {
            switch (spec.params[slot].type)
            {
            case ParamType::F32:
                // A row keeps distances in a uint32 like everything else.
                // Copying the bits and calling the slot a float is the bug the
                // db_scripts round trip found thirty of.
                step.operands[slot].f = float(value);
                break;

            case ParamType::Text:
                // A text id is signed and negative in these tables.
                step.operands[slot].i = int32(value);
                break;

            default:
                step.operands[slot].u = value;
                break;
            }

            // Zero is "not supplied" for an optional parameter, and this is
            // not a convenience: EventAI writes an UNUSED text id as 0, while
            // the DB scripts write it as -1 and treat 0 as a real id. Marking
            // every slot given would turn `talk` with one line into `talk`
            // with one line and two copies of text 0 -- which exists, and is
            // whatever the first row of `db_script_string` happens to be.
            if (value != 0 || !spec.params[slot].optional)
            {
                step.given |= uint8(1u << slot);
            }
        }

        /**
         * MORPH and MOUNT, whose two columns are alternatives.
         *
         * EventAI writes an entry in one and a model id in the other, and zero
         * in both means "undo it". The DB-script verb has ONE field and says
         * which of the two it holds with a flag -- so this is the one place a
         * value moves between a column and a flag, and it is worth the eight
         * lines rather than a third column in the table nothing else uses.
         */
        void LowerAlternate(Step& step, ActionSpec const& spec, uint32 entry,
                            uint32 modelId)
        {
            if (entry)
            {
                Put(step, spec, 0, entry);
            }
            else if (modelId)
            {
                Put(step, spec, 0, modelId);
                step.buddy.flags |= CommandAdditional;
            }
            else
            {
                // Both zero: demorph/dismount, which the body reads as a
                // literal zero rather than as an absent parameter.
                Put(step, spec, 0, 0);
            }
        }

        /**
         * SUMMON_ID and SUMMON_UNIQUE, whose third column is a table lookup.
         *
         * The spawn id names a row in `creature_ai_summons` holding a position
         * and a despawn time, which is exactly the `at` facet plus the verb's
         * second parameter. Resolved HERE, at load, rather than at run time:
         * the table is immutable once read, and a step that carries its own
         * position needs no second lookup on every summon.
         */
        bool LowerSummonId(Step& step, ActionSpec const& spec, uint32 creature,
                           uint32 spawnId, std::string& error)
        {
            CreatureEventAI_Summon_Map const& summons =
                sEventAIMgr.GetCreatureEventAISummonMap();

            CreatureEventAI_Summon_Map::const_iterator found =
                summons.find(spawnId);
            if (found == summons.end())
            {
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer),
                              "summon id %u is not in `creature_ai_summons`",
                              spawnId);
                error = buffer;
                return false;
            }

            CreatureEventAI_Summon const& summon = found->second;

            Put(step, spec, 0, creature);

            // Named Secs and passed to SummonCreature, whose despawn argument
            // is milliseconds. The name is wrong in the schema and has been
            // for as long as the column has existed; the value is what the
            // original passes, unchanged.
            if (summon.SpawnTimeSecs)
            {
                Put(step, spec, 1, summon.SpawnTimeSecs);
            }

            // The `at` facet: x, y, z, o, in the order the generator lays it
            // out, starting after the verb's own two.
            float const at[4] = { summon.position_x, summon.position_y,
                                  summon.position_z, summon.orientation };
            for (std::size_t i = 0; i < 4; ++i)
            {
                std::size_t const slot = spec.own + i;
                if (slot >= MaxOperands)
                {
                    break;
                }
                step.operands[slot].f = at[i];
                step.given |= uint8(1u << slot);
            }

            return true;
        }

        /// EventAI's event_type, as the trigger it means.
        RuleId TriggerOf(uint32 type)
        {
            switch (type)
            {
            case EVENT_T_TIMER_IN_COMBAT:      return RuleId::TimerInCombat;
            case EVENT_T_TIMER_OOC:            return RuleId::TimerOoc;
            case EVENT_T_HP:                   return RuleId::HealthBelow;
            case EVENT_T_MANA:                 return RuleId::ManaBelow;
            case EVENT_T_AGGRO:                return RuleId::Aggro;
            case EVENT_T_KILL:                 return RuleId::KilledUnit;
            case EVENT_T_DEATH:                return RuleId::Died;
            case EVENT_T_EVADE:                return RuleId::Evaded;
            case EVENT_T_SPELLHIT:             return RuleId::HitBySpell;
            case EVENT_T_RANGE:                return RuleId::TargetInRange;
            case EVENT_T_OOC_LOS:              return RuleId::SawUnit;
            case EVENT_T_SPAWNED:              return RuleId::Spawned;
            case EVENT_T_TARGET_HP:            return RuleId::TargetHealthBelow;
            case EVENT_T_TARGET_CASTING:       return RuleId::TargetCasting;
            case EVENT_T_FRIENDLY_HP:          return RuleId::FriendlyHurt;
            case EVENT_T_FRIENDLY_IS_CC:       return RuleId::FriendlyControlled;
            case EVENT_T_FRIENDLY_MISSING_BUFF:
                                          return RuleId::FriendlyMissingBuff;
            case EVENT_T_SUMMONED_UNIT:        return RuleId::SummonedUnit;
            case EVENT_T_TARGET_MANA:          return RuleId::TargetManaBelow;
            case EVENT_T_QUEST_ACCEPT:         return RuleId::QuestAccepted;
            case EVENT_T_QUEST_COMPLETE:       return RuleId::QuestCompleted;
            case EVENT_T_REACHED_HOME:         return RuleId::ReachedHome;
            case EVENT_T_RECEIVE_EMOTE:        return RuleId::ReceivedEmote;
            case EVENT_T_AURA:                 return RuleId::HasAura;
            case EVENT_T_TARGET_AURA:          return RuleId::TargetHasAura;
            case EVENT_T_SUMMONED_JUST_DIED:   return RuleId::SummonDied;
            case EVENT_T_SUMMONED_JUST_DESPAWN:
                                          return RuleId::SummonDespawned;
            case EVENT_T_MISSING_AURA:         return RuleId::MissingAura;
            case EVENT_T_TARGET_MISSING_AURA:  return RuleId::TargetMissingAura;
            case EVENT_T_TIMER_GENERIC:        return RuleId::Timer;
            case EVENT_T_RECEIVE_AI_EVENT:     return RuleId::ReceivedAiEvent;
            case EVENT_T_REACHED_WAYPOINT:     return RuleId::ReachedWaypoint;
            case EVENT_T_ENERGY:               return RuleId::EnergyBelow;
            default:                           return RuleId::None;
            }
        }

        /**
         * The two triggers whose columns the manifest does not describe.
         *
         * Both are questions for rules.manifest, and both are quiet rather
         * than loud if left alone -- which is why they are checked here
         * instead of being discovered by a creature standing still.
         */
        bool TriggerFits(CreatureEventAI_Event const& row, std::string& error)
        {
            if (row.event_type == EVENT_T_REACHED_WAYPOINT)
            {
                error = "reached_waypoint is declared as (waypoint, path) and "
                        "the row holds a POSITION -- EventAI tests a box "
                        "around x/y/z, which is a different question";
                return false;
            }

            if (row.event_type == EVENT_T_RECEIVE_EMOTE &&
                row.receive_emote.conditionValue2 != 0)
            {
                error = "received_emote is declared as (emote, condition, "
                        "value) and this row uses a second condition value";
                return false;
            }

            return true;
        }
    }

    bool Lower(CreatureEventAI_Action const& action, Step& out,
               std::string& error)
    {
        out = Step();

        EventAiVerb const* verb = EventAiVerbOf(action.type);

        if (!verb)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer),
                          "action type %u is not in eventai.map at all",
                          uint32(action.type));
            error = buffer;
            return false;
        }

        if (verb->refused)
        {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "action type %u %s",
                          uint32(action.type), verb->refused);
            error = buffer;
            return false;
        }

        ActionSpec const* spec = SpecOf(verb->action);
        if (!spec || spec->arity > MaxOperands)
        {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer),
                          "%s takes %u parameters and a step holds %u",
                          spec ? spec->name : "?",
                          spec ? uint32(spec->arity) : 0u,
                          uint32(MaxOperands));
            error = buffer;
            return false;
        }

        out.action = spec->id;
        out.buddy.flags = verb->flags;

        uint32 const raw[3] = { action.raw.param1, action.raw.param2,
                                action.raw.param3 };

        // The shapes no column mapping can express, before the loop -- their
        // columns are not slots.
        if (verb->form == FormEntryOrModel)
        {
            LowerAlternate(out, *spec, raw[0], raw[1]);
            return true;
        }

        if (verb->form == FormSummonSpawn)
        {
            out.select = Selector(action.summon_id.target);
            return LowerSummonId(out, *spec, action.summon_id.creatureId,
                                 action.summon_id.spawnId, error);
        }

        for (std::size_t i = 0; i < 3; ++i)
        {
            uint8 const slot = verb->slot[i];

            if (slot == MapUnused)
            {
                continue;
            }

            if (slot == MapSelect)
            {
                if (raw[i] >= SelectEnd)
                {
                    char buffer[128];
                    std::snprintf(buffer, sizeof(buffer),
                                  "%s: %u is not a target selector",
                                  spec->name, raw[i]);
                    error = buffer;
                    return false;
                }
                out.select = Selector(raw[i]);
                continue;
            }

            if (slot >= spec->arity)
            {
                char buffer[192];
                std::snprintf(buffer, sizeof(buffer),
                              "%s: this mapping puts a value in slot %u and "
                              "the manifest gives it %u",
                              spec->name, uint32(slot), uint32(spec->arity));
                error = buffer;
                return false;
            }

            Put(out, *spec, slot, raw[i]);
        }

        if (verb->pinSlot != MapNoPin && verb->pinSlot < spec->arity)
        {
            Put(out, *spec, verb->pinSlot, verb->pinValue);
        }

        return true;
    }

    bool Lower(CreatureEventAI_Event const& row, Rule& out, std::string& error)
    {
        char buffer[192];

        RuleId const trigger = TriggerOf(uint32(row.event_type));
        if (trigger == RuleId::None)
        {
            std::snprintf(buffer, sizeof(buffer),
                          "event type %u is not a MAI trigger",
                          uint32(row.event_type));
            error = buffer;
            return false;
        }

        if (!TriggerFits(row, error))
        {
            return false;
        }

        RuleSpec const* spec = SpecOf(trigger);
        if (!spec || spec->arity > MaxOperands)
        {
            std::snprintf(buffer, sizeof(buffer),
                          "%s takes %u parameters and a rule holds %u",
                          spec ? spec->name : "?",
                          spec ? uint32(spec->arity) : 0u,
                          uint32(MaxOperands));
            error = buffer;
            return false;
        }

        out = Rule();
        out.id = row.event_id;
        out.trigger = trigger;
        out.inversePhaseMask = row.event_inverse_phase_mask;
        out.chance = row.event_chance;
        out.flags = row.event_flags;

        // The four event params, in the order the manifest declares them --
        // which is the order the union's arms have them, the manifest having
        // been written from those arms. This half IS generic; see the note at
        // the top for why the action half cannot be.
        uint32 const raw[4] = { row.raw.param1, row.raw.param2,
                                row.raw.param3, row.raw.param4 };
        for (std::size_t slot = 0; slot < spec->arity && slot < 4; ++slot)
        {
            if (spec->params[slot].type == ParamType::F32)
            {
                out.operands[slot].f = float(raw[slot]);
            }
            else
            {
                out.operands[slot].u = raw[slot];
            }

            if (raw[slot] != 0 || !spec->params[slot].optional)
            {
                out.given |= uint8(1u << slot);
            }
        }

        // The three action slots, as a sequence. All at time zero: that is
        // what they always meant, EventAI running them together and in order.
        for (uint32 i = 0; i < MAX_ACTIONS; ++i)
        {
            CreatureEventAI_Action const& action = row.action[i];
            if (action.type == ACTION_T_NONE)
            {
                continue;
            }

            Step step;
            if (!Lower(action, step, error))
            {
                return false;
            }

            step.atMs = 0;
            out.steps.steps.push_back(step);
        }

        out.steps.id = row.event_id;
        return true;
    }
}
