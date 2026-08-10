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

// When a rule fires.
//
// Three things happen in a fixed order and the order is the whole correctness
// of the file:
//
//   1. the PHASE, which can veto a rule outright -- and, for a timer, must
//      also freeze its clock rather than merely suppressing the firing
//   2. the trigger's own CONDITION, which reads the world, and which re-arms
//      the timer as a side effect precisely BECAUSE it read the world: a
//      health check that finds the creature above the threshold has not
//      happened, and must be asked again on the next tick rather than in
//      thirty seconds
//   3. the CHANCE, rolled once and shared, so a rule with three steps rolls
//      once and not three times
//
// Each of those is a way a plausible rewrite goes wrong, and all three are
// EventAI's own answers. This is a transcription with the four systems it was
// tangled with removed -- not an improvement on them, which comes after there
// is something to compare against.

#include "MaiCreatureAI.h"

#include "MaiExecute.h"
#include "MaiRunner.h"
#include "MaiSelect.h"

#include "Creature.h"
#include "InstanceData.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuras.h"
#include "World.h"

#include <algorithm>
#include <list>

namespace mai
{
    namespace
    {
        /// Rules are looked at this often rather than every tick. EventAI's own
        /// number, and its reason still holds: a boss with forty rules would
        /// otherwise walk all forty at the map's full rate to discover that
        /// nothing is due.
        enum : uint32 { LookEveryMs = 500 };

        /// The marks at which a creature announces how hurt it is, and what it
        /// announces. Both are the original's.
        enum : uint32 { HealthSteps = 3 };

        /// The sentinel m_throwStep reaches when there is nothing left to
        /// announce. Not a percentage and not a count -- the original's own.
        enum : uint32 { ThrowDone = 100 };

        float const AiEventRadius = 30.0f;

        uint32 Percent(uint32 part, uint32 whole)
        {
            return whole ? (part * 100) / whole : 0;
        }
    }

    MaiCreatureAI::MaiCreatureAI(Creature* creature, RuleSet const* rules)
        : CreatureAI(creature)
    {
        if (rules)
        {
            m_armed.reserve(rules->rules.size());

            for (Rule const& rule : rules->rules)
            {
#ifndef MANGOS_DEBUG
                if (rule.flags & RuleDebugOnly)
                {
                    continue;
                }
#endif
                // Which difficulties a rule is for. In a dungeon the spawn
                // mode picks the bit; outside one, a rule that named no
                // difficulty or named normal applies and a heroic-only rule
                // does not.
                bool applies;
                if (m_creature->GetMap()->IsDungeon())
                {
                    applies = (rule.flags &
                               (1 << (m_creature->GetMap()->GetSpawnMode() + 1)))
                              != 0;
                }
                else
                {
                    applies = !(rule.flags &
                                (RuleNormalOnly | RuleHeroicOnly)) ||
                              (rule.flags & RuleNormalOnly) != 0;
                }

                if (!applies)
                {
                    continue;
                }

                Armed armed;
                armed.rule = &rule;
                m_armed.push_back(armed);

                if (rule.trigger == RuleId::SawUnit)
                {
                    m_watchesSight = true;
                }
            }

            if (!rules->rules.empty() && m_armed.empty())
            {
                sLog.outErrorDb("MAI: creature %u has rules but none of them "
                                "apply here (map %u, difficulty %u).",
                                m_creature->GetEntry(), m_creature->GetMapId(),
                                m_creature->GetMap()->GetDifficulty());
            }
        }

        // Sets the timers and runs the spawn rules, exactly as the original's
        // constructor did by calling the same thing.
        JustRespawned();
    }

    // -- when a rule may fire -----------------------------------------------

    bool MaiCreatureAI::Ready(Armed const& armed) const
    {
        if (!armed.enabled || armed.timeMs != 0 || !armed.rule ||
            !m_actor.phases.Allows(armed.rule->inversePhaseMask))
        {
            return false;
        }

        return Allowed(*armed.rule);
    }

    bool MaiCreatureAI::Allowed(Rule const& rule) const
    {
        // The decision, and it comes BEFORE the trigger's own condition on
        // purpose: a guard is about what this creature remembers, which is
        // free to test, while a condition asks the world -- who is on the
        // threat list, what auras are up. A boss that only enrages once should
        // not search the grid every half second to rediscover that.
        for (Guard const& guard : rule.guards)
        {
            uint32 held = 0;

            if (guard.of == GuardInstance)
            {
                InstanceData* data = m_creature->GetMap()->GetInstanceData();
                if (!data)
                {
                    // Outside an instance there is nothing to ask, and a rule
                    // that asked is not satisfied. Refusing rather than
                    // defaulting to zero: zero is a real encounter state --
                    // NOT_STARTED -- so treating "no instance" as zero would
                    // make `instance:6=0` fire in the open world.
                    return false;
                }

                held = data->GetData(guard.subject);
            }
            else if (guard.of == GuardAura || guard.of == GuardTargetAura)
            {
                // Stacks, not presence: absent is zero, so one comparison
                // answers "is it up", "is it gone" and "is it at three".
                Unit const* who = guard.of == GuardAura
                                      ? static_cast<Unit const*>(m_creature)
                                      : m_creature->getVictim();
                if (!who)
                {
                    return false;
                }

                SpellAuraHolder* holder =
                    const_cast<Unit*>(who)->GetSpellAuraHolder(guard.subject);
                held = holder ? holder->GetStackAmount() : 0;
            }
            else
            {
                held = guard.subject < MaxStates
                           ? m_actor.states[guard.subject]
                           : 0;
            }

            if (!guard.Holds(held))
            {
                return false;
            }
        }

        return true;
    }

    bool MaiCreatureAI::ReArm(Armed& armed, std::size_t minSlot,
                              std::size_t maxSlot)
    {
        Rule const& rule = *armed.rule;

        uint32 const least = rule.Param(minSlot);
        uint32 const most = rule.Param(maxSlot);

        if (least == most)
        {
            armed.timeMs = least;
        }
        else if (most > least)
        {
            armed.timeMs = urand(least, most);
        }
        else
        {
            // An inverted pair disables the rule rather than picking one of
            // the two, which is the original's answer and the loud one: a rule
            // that silently repeated at `min` would look like it worked.
            sLog.outErrorDb("MAI: creature %u rule %u has a repeat max below "
                            "its min. Repeating disabled.",
                            m_creature->GetEntry(), rule.id);
            armed.enabled = false;
            return false;
        }

        return true;
    }

    /**
     * The trigger's own condition.
     *
     * Every arm may re-arm the timer, and only after it has decided the
     * condition holds -- see the note at the top of the file. Several also
     * name the unit that made them fire, which is why @a invoker is in and
     * out: a rule watching for a hurt friend is triggered BY that friend.
     */
    bool MaiCreatureAI::Holds(Armed& armed, Unit*& invoker)
    {
        Rule const& rule = *armed.rule;

        switch (rule.trigger)
        {
        case RuleId::TimerInCombat:
            if (!m_creature->IsInCombat())
            {
                return false;
            }
            return ReArm(armed, 2, 3);

        case RuleId::TimerOoc:
            if (m_creature->IsInCombat() || m_creature->IsInEvadeMode())
            {
                return false;
            }
            return ReArm(armed, 2, 3);

        case RuleId::Timer:
            return ReArm(armed, 2, 3);

        case RuleId::HealthBelow:
        {
            if (!m_creature->IsInCombat() || !m_creature->GetMaxHealth())
            {
                return false;
            }

            uint32 const percent = Percent(m_creature->GetHealth(),
                                           m_creature->GetMaxHealth());
            if (percent > rule.Param(0) || percent < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::ManaBelow:
        {
            if (!m_creature->IsInCombat() ||
                !m_creature->GetMaxPower(POWER_MANA))
            {
                return false;
            }

            uint32 const percent = Percent(m_creature->GetPower(POWER_MANA),
                                           m_creature->GetMaxPower(POWER_MANA));
            if (percent > rule.Param(0) || percent < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::EnergyBelow:
        {
            if (!m_creature->IsInCombat() ||
                !m_creature->GetMaxPower(POWER_ENERGY))
            {
                return false;
            }

            uint32 const percent =
                Percent(m_creature->GetPower(POWER_ENERGY),
                        m_creature->GetMaxPower(POWER_ENERGY));
            if (percent > rule.Param(0) || percent < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::TargetHealthBelow:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim || !victim->GetMaxHealth())
            {
                return false;
            }

            uint32 const percent = Percent(victim->GetHealth(),
                                           victim->GetMaxHealth());
            if (percent > rule.Param(0) || percent < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::TargetManaBelow:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim ||
                !victim->GetMaxPower(POWER_MANA))
            {
                return false;
            }

            uint32 const percent = Percent(victim->GetPower(POWER_MANA),
                                           victim->GetMaxPower(POWER_MANA));
            if (percent > rule.Param(0) || percent < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::TargetCasting:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim ||
                !victim->IsNonMeleeSpellCasted(false, false, true))
            {
                return false;
            }
            return ReArm(armed, 0, 1);
        }

        case RuleId::TargetInRange:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim ||
                !m_creature->Where().ShareFrame(victim->Where()))
            {
                return false;
            }

            float const least = rule.Has(0) ? rule.operands[0].f : 0.0f;
            float const most = rule.Has(1) ? rule.operands[1].f : 0.0f;
            if (!m_creature->Where().WithinRange(victim->Where(), least, most))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::HasAura:
        {
            if (!m_creature->IsInCombat())
            {
                return false;
            }

            SpellAuraHolder* holder =
                m_creature->GetSpellAuraHolder(rule.Param(0));
            if (!holder || holder->GetStackAmount() < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::MissingAura:
        {
            if (!m_creature->IsInCombat())
            {
                return false;
            }

            SpellAuraHolder* holder =
                m_creature->GetSpellAuraHolder(rule.Param(0));
            if (holder && holder->GetStackAmount() >= rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::TargetHasAura:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim)
            {
                return false;
            }

            SpellAuraHolder* holder = victim->GetSpellAuraHolder(rule.Param(0));
            if (!holder || holder->GetStackAmount() < rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::TargetMissingAura:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim)
            {
                return false;
            }

            SpellAuraHolder* holder = victim->GetSpellAuraHolder(rule.Param(0));
            if (holder && holder->GetStackAmount() >= rule.Param(1))
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::FriendlyHurt:
        {
            if (!m_creature->IsInCombat())
            {
                return false;
            }

            // Slots are the union's order: how much health is missing, then
            // how far to look. See the note in rules.manifest -- these two
            // were declared the other way round and both are uint32.
            float const radius = rule.Has(1) ? rule.operands[1].f : 0.0f;

            Unit* hurt = nullptr;
            MaNGOS::MostHPMissingInRangeCheck check(m_creature, radius,
                                                    rule.Param(0));
            MaNGOS::UnitLastSearcher<MaNGOS::MostHPMissingInRangeCheck>
                search(hurt, check);
            Cell::VisitGridObjects(m_creature, search, radius);

            if (!hurt)
            {
                return false;
            }

            invoker = hurt;
            return ReArm(armed, 2, 3);
        }

        case RuleId::FriendlyControlled:
        {
            if (!m_creature->IsInCombat())
            {
                return false;
            }

            float const radius = rule.Has(1) ? rule.operands[1].f : 0.0f;

            std::list<Creature*> held;
            MaNGOS::FriendlyCCedInRangeCheck check(m_creature, radius);
            MaNGOS::CreatureListSearcher<MaNGOS::FriendlyCCedInRangeCheck>
                search(held, check);
            Cell::VisitGridObjects(m_creature, search, radius);

            if (held.empty())
            {
                return false;
            }

            // The first will do: the rule asks whether anyone is held, not
            // which of them is worst off.
            invoker = held.front();
            return ReArm(armed, 2, 3);
        }

        case RuleId::FriendlyMissingBuff:
        {
            float const radius = rule.Has(1) ? rule.operands[1].f : 0.0f;

            std::list<Creature*> lacking;
            MaNGOS::FriendlyMissingBuffInRangeCheck check(m_creature, radius,
                                                          rule.Param(0));
            MaNGOS::CreatureListSearcher<
                MaNGOS::FriendlyMissingBuffInRangeCheck> search(lacking, check);
            Cell::VisitGridObjects(m_creature, search, radius);

            if (lacking.empty())
            {
                return false;
            }

            invoker = lacking.front();
            return ReArm(armed, 2, 3);
        }

        case RuleId::SummonedUnit:
        case RuleId::SummonDied:
        case RuleId::SummonDespawned:
        {
            // The summon is the invoker, and the rule may name which entry it
            // cares about.
            if (!invoker || invoker->GetTypeId() != TYPEID_UNIT)
            {
                return false;
            }

            if (static_cast<Creature*>(invoker)->GetEntry() != rule.Param(0))
            {
                return false;
            }
            return ReArm(armed, 1, 2);
        }

        case RuleId::AwayFrom:
        {
            float const range = rule.Has(1) ? rule.operands[1].f : 0.0f;

            // Nobody of that entry within range. The inverse of every other
            // proximity question, and the only one that is true when the
            // search finds NOTHING -- which is why it cannot be written as a
            // buddy: a buddy that is not found stops the step.
            // Not called `near`: windef.h defines that as a macro, and the
            // error it produces names the line after the one that is wrong.
            Creature* company = nullptr;
            MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck check(
                *m_creature, rule.Param(0), true, false, range, true);
            MaNGOS::CreatureLastSearcher<
                MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck>
                    search(company, check);
            Cell::VisitGridObjects(m_creature, search, range);

            if (company)
            {
                return false;
            }
            return ReArm(armed, 2, 3);
        }

        case RuleId::KilledUnit:
            return ReArm(armed, 0, 1);

        case RuleId::HitBySpell:
            // Which spell and which school were checked by the caller: it is
            // the one that knows what hit us.
            return ReArm(armed, 2, 3);

        case RuleId::SawUnit:
            return ReArm(armed, 2, 3);

        // The ones with no condition of their own beyond having happened.
        case RuleId::Aggro:
        case RuleId::Died:
        case RuleId::Evaded:
        case RuleId::Spawned:
        case RuleId::ReachedHome:
        case RuleId::ReceivedEmote:
        case RuleId::ReceivedAiEvent:
        case RuleId::ReachedWaypoint:
        case RuleId::QuestAccepted:
        case RuleId::QuestCompleted:
            return true;

        default:
            sLog.outErrorDb("MAI: creature %u rule %u has trigger %u, which "
                            "nothing here knows how to check.",
                            m_creature->GetEntry(), rule.id,
                            uint32(rule.trigger));
            return true;
        }
    }

    bool MaiCreatureAI::Fire(Armed& armed, Unit* invoker, Creature* sender)
    {
        if (!Ready(armed))
        {
            return false;
        }

        if (!Holds(armed, invoker))
        {
            return false;
        }

        // Non-repeatable rules are spent whether or not the chance comes up.
        // The original's order, and it matters: rolled the other way round, a
        // one-in-ten aggro yell would get ten chances instead of one.
        if (!(armed.rule->flags & RuleRepeatable))
        {
            armed.enabled = false;
        }

        // Zero means never. The loader says so and keeps the rule anyway.
        if (armed.rule->chance == 0 ||
            armed.rule->chance <= urand(0, 99))
        {
            return false;
        }

        Start(*armed.rule, invoker, sender);
        return true;
    }

    void MaiCreatureAI::Fire(RuleId trigger, Unit* invoker, Creature* sender)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger == trigger)
            {
                Fire(armed, invoker, sender);
            }
        }
    }

    // -- running what fired --------------------------------------------------

    void MaiCreatureAI::Start(Rule const& rule, Unit* invoker,
                              Creature* sender)
    {
        if (rule.steps.steps.empty())
        {
            return;
        }

        Frame frame;
        frame.sequence = &rule.steps;
        frame.source = m_creature->GetObjectGuid();
        frame.target = invoker ? invoker->GetObjectGuid() : ObjectGuid();
        frame.sender = sender ? sender->GetObjectGuid() : ObjectGuid();

        if (rule.flags & RuleRandomStep)
        {
            // One step instead of all of them. Every rule carrying this flag
            // came from a table whose three action slots were simultaneous, so
            // there is nothing left to schedule after the one that is chosen
            // and the frame is never kept.
            std::size_t const pick =
                urand(0, uint32(rule.steps.steps.size() - 1));

            Run run;
            run.map = m_creature->GetMap();
            run.source = frame.source;
            run.target = frame.target;
            run.from.invoker = invoker;
            run.from.sender = sender;
            run.actor = &m_actor;
            run.fromRule = true;

            Execute(run, rule.steps.steps[pick]);
            return;
        }

        // Kept and ticked, even when every step is at time zero: the first
        // tick runs all of them and drops it. Running them here instead would
        // be a second execution path that only the common case takes, which is
        // how the two would drift.
        m_frames.push_back(frame);
    }

    void MaiCreatureAI::Advance(uint32 diff)
    {
        if (m_frames.empty())
        {
            return;
        }

        Map* map = m_creature->GetMap();

        // Indexed rather than iterated: a step can start another sequence on
        // this same creature, which appends here.
        std::size_t const was = m_frames.size();
        for (std::size_t i = 0; i < was && i < m_frames.size(); ++i)
        {
            Frame& frame = m_frames[i];

            Run go;
            go.map = map;
            go.source = frame.source;
            go.target = frame.target;
            go.owner = frame.owner;
            go.actor = &m_actor;
            go.fromRule = true;

            // Resolved fresh each tick and never stored: anything a rule named
            // can die between two steps of the sequence that named it.
            go.from.invoker = map->GetUnit(frame.target);
            go.from.sender = map->GetAnyTypeCreature(frame.sender);

            Runner runner(frame, diff);
            while (Step const* step = runner.Next())
            {
                if (Execute(go, *step))
                {
                    runner.Stop();
                }
            }
        }

        m_frames.erase(std::remove_if(m_frames.begin(), m_frames.end(),
                           [](Frame const& frame) { return frame.Finished(); }),
                       m_frames.end());
    }

    void MaiCreatureAI::Tick(uint32 diff)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.timeMs)
            {
                if (armed.timeMs > diff)
                {
                    // A timer does not run down in a phase its rule cannot
                    // fire in. Not an optimisation: a rule that counted while
                    // suppressed would come due the instant the phase changed,
                    // which is how a boss fires four abilities at once on
                    // entering phase two.
                    //
                    // A GUARD suppresses it the same way, and for the same
                    // reason. Golemagg's earthquake is on a three-second timer
                    // that the script only decrements once he has enraged; let
                    // it run underneath and the earthquake lands the instant
                    // he does, which is not the fight anyone wrote.
                    if (m_actor.phases.Allows(armed.rule->inversePhaseMask) &&
                        Allowed(*armed.rule))
                    {
                        armed.timeMs -= diff;
                    }
                }
                else
                {
                    armed.timeMs = 0;
                }
            }

            if (!armed.enabled || armed.timeMs)
            {
                continue;
            }

            switch (armed.rule->trigger)
            {
            // The triggers that ask the world rather than waiting to be told.
            case RuleId::TimerInCombat:
            case RuleId::TimerOoc:
            case RuleId::Timer:
            case RuleId::HealthBelow:
            case RuleId::ManaBelow:
            case RuleId::EnergyBelow:
            case RuleId::TargetHealthBelow:
            case RuleId::TargetManaBelow:
            case RuleId::TargetCasting:
            case RuleId::TargetInRange:
            case RuleId::HasAura:
            case RuleId::MissingAura:
            case RuleId::TargetHasAura:
            case RuleId::TargetMissingAura:
            case RuleId::FriendlyHurt:
            case RuleId::FriendlyControlled:
            case RuleId::FriendlyMissingBuff:
                Fire(armed);
                break;

            default:
                break;
            }
        }
    }

    void MaiCreatureAI::UpdateAI(uint32 diff)
    {
        // Also updates the threat list, which is why it is called even when
        // the answer is thrown away.
        bool const fighting =
            m_creature->SelectHostileTarget() && m_creature->getVictim();

        if (m_untilLookMs < diff)
        {
            m_sinceLookMs += diff;
            Tick(m_sinceLookMs);
            m_sinceLookMs = 0;
            m_untilLookMs = LookEveryMs;
        }
        else
        {
            m_sinceLookMs += diff;
            m_untilLookMs -= diff;
        }

        // The sequences run at the map's own rate rather than at the rule
        // scan's. A rule that says "and three seconds later, this" must be
        // three seconds and not three seconds rounded to the next half.
        Advance(diff);

        // getVictim may have become null inside a step that just ran.
        if (fighting && m_creature->getVictim() && m_actor.meleeAllowed)
        {
            DoMeleeAttackIfReady();
        }
    }

    // -- the triggers --------------------------------------------------------

    void MaiCreatureAI::JustRespawned()
    {
        Reset();

        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger == RuleId::Timer)
            {
                // The generic timer is the one that starts running the moment
                // the creature exists, in or out of combat.
                if (ReArm(armed, 0, 1))
                {
                    armed.enabled = true;
                }
            }
            else if (armed.rule->trigger == RuleId::Spawned)
            {
                bool applies = false;
                switch (armed.rule->Param(0))
                {
                case 0:     // always
                    applies = true;
                    break;

                case 1:     // this map
                    applies = m_creature->GetMapId() == armed.rule->Param(1);
                    break;

                case 2:     // this zone or area
                {
                    uint32 zone = 0;
                    uint32 area = 0;
                    m_creature->GetTerrain()->GetZoneAndAreaId(
                        zone, area, m_creature->Where().X(),
                        m_creature->Where().Y(), m_creature->Where().Z());
                    applies = zone == armed.rule->Param(1) ||
                              area == armed.rule->Param(1);
                    break;
                }

                default:
                    break;
                }

                if (applies)
                {
                    Fire(armed);
                }
            }
        }
    }

    void MaiCreatureAI::Reset()
    {
        m_untilLookMs = LookEveryMs;
        m_sinceLookMs = 0;
        m_throwStep = 0;

        // Anything still running is about a fight that is over.
        m_frames.clear();

        for (Armed& armed : m_armed)
        {
            // Only the out-of-combat timers are re-armed here. The rest keep
            // whatever state they were left in, which is the original's
            // behaviour and the reason an aggro yell does not repeat when a
            // creature resets: it was disabled on first use and nothing here
            // enables it again.
            if (armed.rule->trigger == RuleId::TimerOoc)
            {
                if (ReArm(armed, 0, 1))
                {
                    armed.enabled = true;
                }
            }
        }
    }

    void MaiCreatureAI::EnterCombat(Unit* enemy)
    {
        for (Armed& armed : m_armed)
        {
            switch (armed.rule->trigger)
            {
            case RuleId::Aggro:
                armed.enabled = true;
                Fire(armed, enemy);
                break;

            case RuleId::TimerInCombat:
                if (ReArm(armed, 0, 1))
                {
                    armed.enabled = true;
                }
                break;

            default:
                // Everything else starts the fight armed and due.
                armed.enabled = true;
                armed.timeMs = 0;
                break;
            }
        }

        m_untilLookMs = LookEveryMs;
        m_sinceLookMs = 0;
    }

    void MaiCreatureAI::EnterEvadeMode()
    {
        m_creature->RemoveAllAurasOnEvade();
        m_creature->DeleteThreatList();
        m_creature->CombatStop(true);

        if (m_creature->IsAlive())
        {
            m_creature->GetMotionMaster()->MoveTargetedHome();
        }

        m_creature->SetLootRecipient(nullptr);

        Fire(RuleId::Evaded);

        m_creature->ResetPlayerDamageReq();
    }

    void MaiCreatureAI::JustReachedHome()
    {
        Fire(RuleId::ReachedHome);
        Reset();
    }

    void MaiCreatureAI::JustDied(Unit* killer)
    {
        Reset();

        if (m_creature->IsGuard() && killer)
        {
            if (Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                m_creature->SendZoneUnderAttackMessage(player);
            }
        }

        if (m_actor.throwMask & (1 << AI_EVENT_JUST_DIED))
        {
            SendAIEventAround(AI_EVENT_JUST_DIED, killer, 0, AiEventRadius);
        }

        Fire(RuleId::Died, killer);

        // After the death rules, not before: one of them may have wanted to
        // know which phase the creature died in.
        m_actor.phases.current = 0;
    }

    void MaiCreatureAI::KilledUnit(Unit* victim)
    {
        // Players only, as the original had it.
        if (!victim || victim->GetTypeId() != TYPEID_PLAYER)
        {
            return;
        }

        Fire(RuleId::KilledUnit, victim);
    }

    void MaiCreatureAI::JustSummoned(Creature* summoned)
    {
        Fire(RuleId::SummonedUnit, summoned);
    }

    void MaiCreatureAI::SummonedCreatureJustDied(Creature* summoned)
    {
        Fire(RuleId::SummonDied, summoned);
    }

    void MaiCreatureAI::SummonedCreatureDespawn(Creature* summoned)
    {
        Fire(RuleId::SummonDespawned, summoned);
    }

    void MaiCreatureAI::ReceiveAIEvent(AIEventType type, Creature* sender,
                                       Unit* invoker, uint32 misc)
    {
        if (!sender)
        {
            return;
        }

        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger != RuleId::ReceivedAiEvent ||
                armed.rule->Param(0) != uint32(type))
            {
                continue;
            }

            // A rule may name which creature it will listen to.
            uint32 const from = armed.rule->Param(1);
            if (from && from != sender->GetEntry())
            {
                continue;
            }

            // And may insist on the value the sender chose. Absent means any,
            // which is what every converted row has -- EventAI's own third and
            // fourth columns on this event were unused.
            if (armed.rule->Has(2) && armed.rule->Param(2) != misc)
            {
                continue;
            }

            Fire(armed, invoker, sender);
        }
    }

    void MaiCreatureAI::SpellHit(Unit* caster, SpellEntry const* spell)
    {
        if (!spell)
        {
            return;
        }

        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger != RuleId::HitBySpell)
            {
                continue;
            }

            // Either the rule names a spell and this is it, or it names none
            // and any will do -- and the school must overlap either way.
            uint32 const wanted = armed.rule->Param(0);
            if (wanted && spell->ID != wanted)
            {
                continue;
            }

            if (!(spell->SchoolMask & armed.rule->Param(1)))
            {
                continue;
            }

            Fire(armed, caster);
        }
    }

    void MaiCreatureAI::ReceiveEmote(Player* player, uint32 emote)
    {
        if (!player)
        {
            return;
        }

        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger != RuleId::ReceivedEmote ||
                armed.rule->Param(0) != emote)
            {
                continue;
            }

            PlayerCondition condition(0, armed.rule->Param(1),
                                      armed.rule->Param(2), 0);
            if (condition.Meets(player, m_creature->GetMap(), m_creature,
                                CONDITION_FROM_EVENTAI))
            {
                Fire(armed, player);
            }
        }
    }

    void MaiCreatureAI::MovementInform(uint32 type, uint32 pointId)
    {
        if (type != WAYPOINT_MOTION_TYPE && type != POINT_MOTION_TYPE)
        {
            return;
        }

        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger != RuleId::ReachedWaypoint)
            {
                continue;
            }

            // The rule may name a node, or fire at every one of them.
            if (armed.rule->Has(0) && armed.rule->Param(0) != pointId)
            {
                continue;
            }

            Fire(armed);
        }
    }

    void MaiCreatureAI::AttackStart(Unit* who)
    {
        if (!who)
        {
            return;
        }

        if (m_creature->Attack(who, m_actor.meleeAllowed))
        {
            m_creature->AddThreat(who);
            m_creature->SetInCombatWith(who);
            who->SetInCombatWith(m_creature);

            HandleMovementOnAttackStart(who);
        }
    }

    void MaiCreatureAI::MoveInLineOfSight(Unit* who)
    {
        if (!who)
        {
            return;
        }

        // The hottest callback in the server, so the common case -- no rule
        // watching -- is a bool test and nothing else.
        if (m_watchesSight && !m_creature->getVictim())
        {
            for (Armed& armed : m_armed)
            {
                if (armed.rule->trigger != RuleId::SawUnit)
                {
                    continue;
                }

                // A rule watches for a friend or for an enemy, never both.
                bool const wantsFriendly = armed.rule->Param(0) != 0;
                if (wantsFriendly == m_creature->IsHostileTo(who))
                {
                    continue;
                }

                float const range = armed.rule->Has(1)
                                        ? armed.rule->operands[1].f
                                        : 0.0f;
                if (InReach(*m_creature, *who, range) &&
                    HasLineOfSight(*m_creature, *who))
                {
                    Fire(armed, who);
                }
            }
        }

        if (m_creature->IsCivilian() || m_creature->IsNeutralToAll())
        {
            return;
        }

        if (m_creature->CanInitiateAttack() && who->IsTargetableForAttack() &&
            m_creature->IsHostileTo(who) &&
            who->isInAccessablePlaceFor(m_creature))
        {
            if (!m_creature->CanFly() &&
                m_creature->Where().HeightGapTo(who->Where()) >
                    CREATURE_Z_ATTACK_RANGE)
            {
                return;
            }

            float const attackRadius = m_creature->GetAttackDistance(who);
            if (InReach(*m_creature, *who, attackRadius) &&
                HasLineOfSight(*m_creature, *who))
            {
                if (!m_creature->getVictim())
                {
                    AttackStart(who);
                    who->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
                }
                else if (m_creature->GetMap()->IsDungeon())
                {
                    m_creature->AddThreat(who);
                    who->SetInCombatWith(m_creature);
                }
            }
        }
    }

    void MaiCreatureAI::DamageTaken(Unit* dealer, uint32& damage)
    {
        // Invincibility first: a creature that refuses to drop below a health
        // must refuse before anything is told how hurt it is.
        if (m_actor.invincibilityHp > 0)
        {
            uint32 const floorHp =
                m_actor.invincibilityIsPercent
                    ? (m_creature->GetMaxHealth() *
                       m_actor.invincibilityHp) / 100
                    : m_actor.invincibilityHp;

            if (m_creature->GetHealth() < floorHp + damage)
            {
                damage = m_creature->GetHealth() <= floorHp
                             ? 0
                             : m_creature->GetHealth() - floorHp;
            }
        }

        if (!m_actor.throwMask || !m_creature->GetMaxHealth())
        {
            return;
        }

        uint32 step = m_throwStep != ThrowDone ? m_throwStep : 0;
        if (step >= HealthSteps)
        {
            return;
        }

        float const marks[HealthSteps] = { 90.0f, 50.0f, 10.0f };
        AIEventType const events[HealthSteps] = { AI_EVENT_LOST_SOME_HEALTH,
                                                  AI_EVENT_LOST_HEALTH,
                                                  AI_EVENT_CRITICAL_HEALTH };

        float const after = (m_creature->GetHealth() - damage) * 100.0f /
                            m_creature->GetMaxHealth();
        if (after > marks[step])
        {
            return;
        }

        // A single large hit can pass several marks at once; announce the
        // lowest one reached that anything is listening for.
        for (uint32 i = HealthSteps - 1; i > step; --i)
        {
            if (after < marks[i] && (m_actor.throwMask & (1 << events[i])))
            {
                step = i;
                break;
            }
        }

        if (m_actor.throwMask & (1 << events[step]))
        {
            SendAIEventAround(events[step], dealer, 0, AiEventRadius);
        }

        m_throwStep = step + 1;
    }

    void MaiCreatureAI::HealedBy(Unit* healer, uint32& healed)
    {
        if (m_throwStep == ThrowDone)
        {
            return;
        }

        if (m_creature->GetHealth() + healed >= m_creature->GetMaxHealth())
        {
            if (m_actor.throwMask & (1 << AI_EVENT_GOT_FULL_HEALTH))
            {
                SendAIEventAround(AI_EVENT_GOT_FULL_HEALTH, healer, 0,
                                  AiEventRadius);
            }
            m_throwStep = ThrowDone;
        }
    }

    bool MaiCreatureAI::IsVisible(Unit* who) const
    {
        return who &&
               m_creature->Where().WithinDist(
                   who->Where(), sWorld.getConfig(CONFIG_FLOAT_SIGHT_MONSTER)) &&
               who->IsVisibleForOrDetect(m_creature, m_creature, true);
    }

    void MaiCreatureAI::GetAIInformation(ChatHandler& reader)
    {
        reader.PSendSysMessage("MAI: phase %u, %u rules, %u running",
                               m_actor.phases.current,
                               uint32(m_armed.size()),
                               uint32(m_frames.size()));
    }
}
