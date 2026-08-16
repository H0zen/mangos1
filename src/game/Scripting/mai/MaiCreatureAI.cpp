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
// For sSpellStore, which a guard about the spell that set a proc off has to
// read the class mask out of.
#include "DBCStores.h"
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
        /**
         * How often the rules are looked at.
         *
         * EventAI used 500, and the reason given was that a boss with forty
         * rules should not walk all forty at the map's full rate to discover
         * that nothing is due. That reason is weaker than it sounds: the walk
         * itself decrements a timer and tests a phase, and every rule that
         * would do real work is gated behind a timer that is not yet zero.
         *
         * What 500 actually bought was a HALF-SECOND OF SLOP on everything.
         * A health threshold is crossed and noticed up to half a second later;
         * an ability due at seven seconds fires somewhere in [7.0, 7.5]. That
         * was the last difference between MAI and ScriptDev, which tests
         * everything on every tick, and it was the only one that could not be
         * fixed by saying something new in a rule.
         *
         * WHAT IT COSTS. Three triggers query the grid and do NOT re-arm when
         * they find nobody -- friendly_hurt, friendly_controlled,
         * friendly_missing_buff -- so those search once per scan while their
         * condition is unmet. On this world that is 291 rules across 220
         * creature entries, and they now search ten times as often.
         *
         * That is affordable because it is what the scripts being matched
         * already do: ScriptDev's own healers run that search every tick and
         * always have. Ten times EventAI is one times ScriptDev.
         */
        enum : uint32 { LookEveryMs = 50 };

        /// The marks at which a creature announces how hurt it is, and what it
        /// announces. Both are the original's.
        enum : uint32 { HealthSteps = 3 };

        /// The sentinel m_throwStep reaches when there is nothing left to
        /// announce. Not a percentage and not a count -- the original's own.
        enum : uint32 { ThrowDone = 100 };

        float const AiEventRadius = 30.0f;

        /// One of them. EventAI always took the first and 20,732 rules rest on
        /// that; ScriptDev scripts routinely take a random one, and the two
        /// are visibly different when the same friend keeps being chosen.
        Creature* Pick(std::list<Creature*>& found, bool random)
        {
            if (found.empty())
            {
                return nullptr;
            }
            if (!random)
            {
                return found.front();
            }

            std::list<Creature*>::iterator at = found.begin();
            std::advance(at, urand(0, uint32(found.size() - 1)));
            return *at;
        }

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

    bool MaiCreatureAI::Ask(Guard const& guard, uint32& held) const
    {
        if (guard.of == GuardProcSpell)
        {
            // Zero and answerable, unlike the family below: "no spell at all"
            // is what a white swing is, and it is half of what the handlers
            // ask about.
            held = m_procSpell;
            return true;
        }

        if (guard.of == GuardProcFamily)
        {
            // Unanswerable outside a proc's own steps, which is the honest
            // answer rather than zero: read as "bit 42 is clear" it would say
            // something definite about a spell that never existed.
            if (m_procSpell == 0)
            {
                return false;
            }

            SpellEntry const* spell = sSpellStore.LookupEntry(m_procSpell);
            if (!spell)
            {
                return false;
            }

            held = spell->SpellClassMask.IsFitToFamilyMask(
                       UI64LIT(1) << guard.subject) ? 1u : 0u;
            return true;
        }

        if (guard.of == GuardInstance)
        {
            InstanceData* data = m_creature->GetMap()->GetInstanceData();
            if (!data)
            {
                // Outside an instance there is nothing to ask, and whatever
                // asked is not satisfied. Refusing rather than defaulting to
                // zero: zero is a real encounter state -- NOT_STARTED -- so
                // treating "no instance" as zero would make `instance:6=0`
                // hold in the open world.
                return false;
            }

            held = data->GetData(guard.subject);
            return true;
        }

        if (guard.of == GuardAura || guard.of == GuardTargetAura)
        {
            // Stacks, not presence: absent is zero, so one comparison answers
            // "is it up", "is it gone" and "is it at three".
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
            return true;
        }

        if (guard.of == GuardSourceClass)
        {
            // Whoever is acting here is this creature, and a creature is not a
            // player -- which is precisely what a class of zero says. Answered
            // rather than refused: it is a fact about the actor, not a
            // question that cannot be put.
            held = 0;
            return true;
        }

        if (guard.of == GuardReputation)
        {
            // And a creature stands nowhere with anyone. Unanswerable rather
            // than REP_HATED, which would be a definite claim about a standing
            // that does not exist.
            return false;
        }

        if (guard.of == GuardTargetIsSelf ||
            guard.of == GuardTargetFriendly ||
            guard.of == GuardTargetClass)
        {
            // A rule's target is whoever the creature is fighting.
            Unit* to = m_creature->getVictim();

            if (guard.of == GuardTargetIsSelf)
            {
                held = (to == static_cast<Unit const*>(m_creature)) ? 1u : 0u;
                return true;
            }

            if (!to)
            {
                return false;
            }

            if (guard.of == GuardTargetFriendly)
            {
                held = m_creature->IsFriendlyTo(to) ? 1u : 0u;
                return true;
            }

            held = to->GetTypeId() == TYPEID_PLAYER
                 ? uint32(static_cast<Player*>(to)->getClass())
                 : 0u;
            return true;
        }

        if (guard.of == GuardPhase)
        {
            // The number `set_phase` writes, which is not a state slot and
            // could not be read until it had a GuardOf of its own.
            held = m_actor.phases.current;
            return true;
        }

        // The creature's own memory. A slot out of range is a load that went
        // wrong rather than a state of zero, so it is unanswerable too.
        if (guard.subject >= MaxStates)
        {
            return false;
        }

        held = m_actor.states[guard.subject];
        return true;
    }

    bool MaiCreatureAI::Allowed(Rule const& rule) const
    {
        // The decision, and it comes BEFORE the trigger's own condition on
        // purpose: a guard is about what this creature remembers, which is
        // free to test, while a condition asks the world -- who is on the
        // threat list, what auras are up. A boss that only enrages once should
        // not search the grid every half second to rediscover that.
        //
        // The comparison itself is in MaiGuard.h, shared with the guard on a
        // STEP -- which is what makes `if` a verb rather than a second dialect:
        // a rule deciding whether to fire and a step deciding whether to run
        // ask the same question of the same creature through the same code.
        //
        // Qualified, and it has to be: this class has its own `Holds`, which
        // asks whether a TRIGGER's condition is met. Unqualified, the member
        // hides the namespace function outright -- class scope is searched
        // first and does not fall through on a bad signature.
        return mai::Holds(rule.guards.data(), rule.guards.size(), this);
    }

    bool MaiCreatureAI::ReArm(Armed& armed, std::size_t minSlot,
                              std::size_t maxSlot)
    {
        Rule const& rule = *armed.rule;

        uint32 const least = rule.Param(minSlot);

        // An absent max is the min, not zero. Every one of these pairs is
        // declared optional in the manifests, and reading the missing half as
        // 0 made it worse than useless: 0 is below any min, so the branch
        // below called it inverted and DISABLED the rule. A hand-written
        //
        //     timer_in_combat  initial=4000 repeat=7000 repeat_max=11000
        //
        // has no `initial_max`, so its very first arming refused it and the
        // rule never fired at all -- reported once, in a line about a repeat
        // range, for a rule whose repeat range was fine.
        //
        // Converted rows never showed it because EventAI's table has four
        // columns and always writes all four. Only a rule written by hand,
        // which is the whole point of the parameters being optional, could
        // reach it.
        //
        // An explicitly inverted pair is still an error: that is somebody
        // saying two things and meaning neither.
        uint32 const most = rule.Has(maxSlot) ? rule.Param(maxSlot) : least;

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

        case RuleId::VictimOutOfMelee:
        {
            Unit* victim = m_creature->getVictim();
            if (!m_creature->IsInCombat() || !victim ||
                InMeleeReach(*m_creature, *victim))
            {
                return false;
            }
            return ReArm(armed, 0, 1);
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

            invoker = Pick(held, rule.Param(5) != 0);
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

            invoker = Pick(lacking, rule.Param(5) != 0);
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

            // MAY name it. The entry is optional and the conversion drops an
            // optional zero, so a rule that cares about any summon at all has
            // no operand in slot 0 -- and comparing the entry against the zero
            // that leaves behind is a test no creature can pass. `saw_unit`
            // asks the same question with Has(4) and always did.
            if (rule.Has(0) &&
                static_cast<Creature*>(invoker)->GetEntry() != rule.Param(0))
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

        case RuleId::SpellHitTarget:
            // Same shape, one parameter fewer: there is no school to check
            // when the spell is our own.
            return ReArm(armed, 1, 2);

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
            // Refused, not allowed through. A trigger nothing here can check
            // is a rule whose condition is unknown, and firing it would be
            // guessing that the condition was true -- on a creature whose
            // rules somebody has just mistyped.
            sLog.outErrorDb("MAI: creature %u rule %u has trigger %u, which "
                            "nothing here knows how to check.",
                            m_creature->GetEntry(), rule.id,
                            uint32(rule.trigger));
            return false;
        }
    }

    bool MaiCreatureAI::Fire(Armed& armed, Unit* invoker, Creature* sender,
                             bool now, Combat::PointsInputs const* numbers,
                             uint32 procSpell)
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

        Start(*armed.rule, invoker, sender, now, numbers, procSpell);
        return true;
    }

    void MaiCreatureAI::Fire(RuleId trigger, Unit* invoker, Creature* sender,
                             bool now)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger == trigger)
            {
                Fire(armed, invoker, sender, now);
            }
        }
    }

    // -- running what fired --------------------------------------------------

    void MaiCreatureAI::Start(Rule const& rule, Unit* invoker,
                              Creature* sender, bool now,
                              Combat::PointsInputs const* numbers,
                              uint32 procSpell)
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
        frame.procSpell = procSpell;

        if (numbers)
        {
            frame.numbers = *numbers;
        }

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
            run.driver = this;
            run.useSelectors = true;
            run.numbers = frame.numbers;

            // The one path that runs a step without the runner, so it is also
            // the one that would silently ignore the step's guard. A guard has
            // to mean the same thing wherever a step is run from, or `if` for
            // one line means "usually".
            //
            // Which is why the proc spell is put where the Sight can find it
            // here too: this walk has a frame it never queues, so nothing else
            // would ever carry it.
            uint32 const wasProcSpell = m_procSpell;
            m_procSpell = procSpell;

            Step const& chosen = rule.steps.steps[pick];
            if (mai::Holds(rule.steps, chosen, this))
            {
                Execute(run, chosen);
            }

            m_procSpell = wasProcSpell;
            return;
        }

        // Kept and ticked, even when every step is at time zero: the first
        // tick runs all of them and drops it. Running them here instead would
        // be a second execution path that only the common case takes, which is
        // how the two would drift -- which is why `now` below does not open
        // one. It queues the frame exactly as this does and then walks it
        // through RunFrame, the same walk a tick makes, with no time passed.
        m_frames.push_back(frame);

        if (!now)
        {
            return;
        }

        // Unless there is no next tick. A creature that has just died is no
        // longer ALIVE and one walking home is in evade mode, and
        // Creature::Update skips the AI in both -- so the frame just queued
        // would sit there until a Reset threw it away. It is the SAME path,
        // walked at once with no time passed: everything at zero runs, in
        // order, and anything later stays queued for whoever ticks next.
        std::size_t const mine = m_frames.size() - 1;
        RunFrame(mine, 0);

        // AND WHOEVER TICKS NEXT IS NOT THIS CREATURE.
        //
        // "Anything later stays queued" was the half of that sentence which
        // was not true. The steps at time zero ran; the ones with a time on
        // them were queued on an AI object that Creature::Update had already
        // stopped calling, and the Reset that follows a death threw them away
        // a moment later. A death rule could say things but could not wait,
        // and neither could an evade rule -- which is frozen for the whole
        // walk home and then dropped by JustReachedHome.
        //
        // So the rest of the sequence changes OWNER. The map's own schedule
        // ticks whatever happens to the creature, which is exactly what
        // `dbscripts_on_creature_death` has always been and why that one kind
        // of death script did work. The steps point into `mai_rule`, which is
        // loaded once and never rebuilt, so the pointer outlives the corpse by
        // construction -- and the creature's state travels with them, so a
        // guard on a death step still means what it meant when it died.
        //
        // The test is Creature::Update's own, written out rather than guessed
        // at: not alive, or walking home. A `reached_home` rule -- the third
        // one that fires with `now` -- is neither, and keeps its frame, which
        // is right: that creature is standing in its spawn point being ticked.
        if (mine < m_frames.size() && !m_frames[mine].Finished() &&
            (!m_creature->IsAlive() || m_creature->IsInEvadeMode()))
        {
            Frame handed = m_frames[mine];
            handed.actor = m_actor;
            handed.hasActor = true;

            AdoptFrame(m_creature->GetMap(), handed);
            m_frames[mine] = Frame();
        }

        Sweep();
    }

    void MaiCreatureAI::Arm(uint32 id, uint32 ms, bool enable)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.rule->id != id)
            {
                continue;
            }

            armed.enabled = enable;
            armed.timeMs = enable ? ms : 0;
            return;
        }

        // A rule this creature does not have. Worth saying so -- it is always
        // a mistyped id -- and not worth more than saying so.
        sLog.outErrorDb("MAI: creature %u has no rule %u to arm",
                        m_creature ? m_creature->GetEntry() : 0, id);
    }

    void MaiCreatureAI::Advance(uint32 diff)
    {
        if (m_frames.empty())
        {
            return;
        }

        // Indexed rather than iterated: a step can start another sequence on
        // this same creature, which appends here.
        std::size_t const was = m_frames.size();
        for (std::size_t i = 0; i < was && i < m_frames.size(); ++i)
        {
            RunFrame(i, diff);
        }

        Sweep();
    }

    /**
     * One frame, by index.
     *
     * BY INDEX AND ON A COPY, and both halves of that are load-bearing. A step
     * runs arbitrary world code, and two things it can do put the frame this
     * loop was walking somewhere else:
     *
     *   * START ANOTHER SEQUENCE on this creature -- a branch, or a rule fired
     *     by something the step did -- which push_backs into m_frames and
     *     reallocates it. A `Frame&` taken before the call, and the Runner
     *     holding it, then point into the freed buffer.
     *
     *   * END THE CREATURE. `die` reaches DealDamage, which reaches JustDied,
     *     which resets -- and the reset cancels every frame, including this
     *     one. The step returns false, so the loop would ask the runner for
     *     the next step of a sequence about a fight that is over.
     *
     * The copy takes both away: nothing anything does can move it. What is
     * left is telling whether the frame is still WANTED, which is the one
     * question the vector still has to answer, and it answers it by index.
     */
    void MaiCreatureAI::RunFrame(std::size_t index, uint32 diff)
    {
        if (index >= m_frames.size())
        {
            return;
        }

        // BEFORE ANYTHING FOLLOWS THE POINTER, and `Finished()` follows it. A
        // branch points into the SHARED table, and `.reload mai_script` frees
        // every sequence in it -- the engine drops its own frames and has no
        // way to reach this one. The stamp answers "is that pointer still
        // good" without dereferencing what it is asking about, which is the
        // only question left once the answer might be no.
        if (m_frames[index].stamp != 0 &&
            m_frames[index].stamp != SequenceStamp())
        {
            sLog.outErrorDb("MAI: creature %u was running a branch that a "
                            "reload of `mai_script` replaced; dropped.",
                            m_creature->GetEntry());
            m_frames[index] = Frame();
            return;
        }

        if (m_frames[index].Finished())
        {
            return;
        }

        // A step can start a sequence that runs a step, and now that some of
        // them run in place rather than next tick, that nesting is a STACK
        // rather than a queue: a repeatable `evaded` rule whose step calls
        // `evade` would recurse until the stack ran out. Queued, the same
        // mistake merely spun once per tick and was survivable. Eight is far
        // past anything a real script does and short of anything dangerous.
        enum : uint32 { MaxNesting = 8 };
        if (m_running >= MaxNesting)
        {
            sLog.outErrorDb("MAI: creature %u nested sequences %u deep; one of "
                            "its rules starts something that starts it again.",
                            m_creature->GetEntry(), MaxNesting);
            return;
        }

        Frame frame = m_frames[index];

        // Whether anything in this frame was refused rather than done.
        // Only a rule with a retry cares, and finding out costs a bool.
        bool refused = false;

        Map* map = m_creature->GetMap();

        Run go;
        go.map = map;
        go.source = frame.source;
        go.target = frame.target;
        go.owner = frame.owner;
        go.item = frame.item;
        go.origin = frame.sequence ? frame.sequence->origin : 0;
        go.actor = &m_actor;
        go.driver = this;
        go.refused = &refused;
        go.useSelectors = true;
        go.numbers = frame.numbers;

        // Resolved fresh each tick and never stored: anything a rule named
        // can die between two steps of the sequence that named it.
        go.from.invoker = map->GetUnit(frame.target);
        go.from.sender = map->GetAnyTypeCreature(frame.sender);

        ++m_running;

        uint32 const wasProcSpell = m_procSpell;
        m_procSpell = frame.procSpell;

        bool cancelled = false;

        // `this` is the Sight: a guard on a step is answered out of this
        // creature's own states, phase and victim, which is the same place a
        // guard on a rule is answered from.
        Runner runner(frame, diff, this);
        while (Step const* step = runner.Next())
        {
            if (Execute(go, *step))
            {
                runner.Stop();
            }

            // Marked by DropFrames, from inside the step that just ran.
            if (!m_frames[index].sequence)
            {
                cancelled = true;
                break;
            }
        }

        --m_running;
        m_procSpell = wasProcSpell;

        if (cancelled)
        {
            return;
        }

        m_frames[index] = frame;

        if (runner.Exhausted())
        {
            // The frame kept its place and carries on next tick -- it is not
            // an error and the sequence has not ended. Said out loud because
            // the only way here is a loop that is not advancing, and a
            // creature quietly spending a tick's worth of steps on one for
            // ever is exactly what nobody notices.
            sLog.outErrorDb("MAI: creature %u ran %u steps in one tick without "
                            "finishing; a loop in one of its rules is not "
                            "advancing.", m_creature->GetEntry(),
                            uint32(MaxStepsPerTick));
        }

        if (refused)
        {
            Retry(frame);
        }
    }

    void MaiCreatureAI::DropFrames()
    {
        if (!m_running)
        {
            m_frames.clear();
            return;
        }

        // A step is walking these right now. Marking is what a clear cannot
        // be here -- and it is also what lets the rules fired IMMEDIATELY
        // after a drop survive it, since those are appended afterwards and
        // are not marked.
        for (Frame& frame : m_frames)
        {
            frame.sequence = nullptr;
        }
    }

    void MaiCreatureAI::Sweep()
    {
        if (m_running)
        {
            return;
        }

        // The stale test comes FIRST, and short-circuits, because Finished()
        // follows the pointer a stale frame is holding. RunFrame clears them
        // on the way past, so by the time a sweep normally runs there are none
        // left -- but a sweep can also happen without a walk in front of it,
        // and one dangling read is all it takes.
        m_frames.erase(std::remove_if(m_frames.begin(), m_frames.end(),
                           [](Frame const& frame)
                           {
                               return (frame.stamp != 0 &&
                                       frame.stamp != SequenceStamp()) ||
                                      frame.Finished();
                           }),
                       m_frames.end());
    }

    /**
     * A refused cast asks its rule to come round again sooner.
     *
     * ScriptDev re-arms only on success, so a cast that was refused -- already
     * casting, silenced, out of range -- is retried on the very next tick. MAI
     * re-arms on FIRING, which is EventAI's rule and what 20,732 converted
     * rules rest on, so the whole interval is lost instead.
     *
     * `retry` is the difference written down. A rule that sets one gets
     * ScriptDev's persistence with an interval somebody chose; a rule that does
     * not is unchanged, which is every rule the conversion produced.
     *
     * The owning rule is found by its sequence rather than remembered on the
     * frame: a creature has tens of rules, the scan is over a small vector, and
     * it costs nothing on the overwhelmingly common path where nothing was
     * refused at all.
     */
    void MaiCreatureAI::Retry(Frame const& frame)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.rule && &armed.rule->steps == frame.sequence &&
                armed.rule->retryMs)
            {
                // Re-ARMED, not merely re-timed, and that is the whole of what
                // a retry means on a rule that fires once. A cast that was
                // refused did not happen, so the rule is not spent: Emeriss
                // corrupts the earth at three-quarters health and would
                // otherwise never do it again because he happened to be
                // mid-cast at the moment he crossed the line.
                armed.enabled = true;
                armed.timeMs = armed.rule->retryMs;
                return;
            }
        }
    }

    void MaiCreatureAI::Tick(uint32 diff)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.timeMs)
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
                //
                // THE LAST TICK COUNTS TOO. Written as "count down, else set
                // to zero", the freeze let go of every timer with less than a
                // tick left on it: a suppressed rule with 30ms to run came due
                // anyway, and the whole point of the freeze -- that a phase
                // change does not empty a boss's cooldowns at once -- was lost
                // for exactly the rules nearest to firing.
                if (m_actor.phases.Allows(armed.rule->inversePhaseMask) &&
                    Allowed(*armed.rule))
                {
                    armed.timeMs = armed.timeMs > diff ? armed.timeMs - diff
                                                       : 0;
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
            case RuleId::VictimOutOfMelee:
            case RuleId::TargetCasting:
            case RuleId::TargetInRange:
            case RuleId::HasAura:
            case RuleId::MissingAura:
            case RuleId::TargetHasAura:
            case RuleId::TargetMissingAura:
            case RuleId::FriendlyHurt:
            case RuleId::FriendlyControlled:
            case RuleId::FriendlyMissingBuff:

            // The inverse question, and it asks the world exactly as the rest
            // of them do -- "is nobody of that entry near me" is still a grid
            // search. Left out of this list it was a rule that loaded, armed
            // itself, and was never once asked: every separation-anxiety add
            // in the world simply did not have the behaviour.
            case RuleId::AwayFrom:
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
        DropFrames();

        // And so is everything the fight wrote down. MaiActor says as much
        // beside each field -- `enraged=0` means "not in THIS fight", a focus
        // does not survive a wipe -- and nothing was delivering it: the states,
        // the remembered target, the invincibility floor, the throw mask and
        // both AI switches were set once and kept for the life of the
        // creature. A boss that enraged at twenty per cent could not enrage
        // again after a wipe, and one whose script had turned melee off in its
        // last phase spent the rest of its existence refusing to swing.
        m_actor.Reset();

        // The base class's half of the same switch. m_actor.combatMovement is
        // what a rule reads and this is what the WORLD reads, and they are one
        // switch: leaving this one alone would resume the fight with the AI
        // still refusing to chase.
        AddCombatMovementFlags(COMBAT_MOVEMENT_SCRIPT);
        m_attackDistance = 0.0f;
        m_attackAngle = 0.0f;

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

            case RuleId::FriendlyHurt:
            case RuleId::FriendlyControlled:
            case RuleId::FriendlyMissingBuff:
                // The only triggers with an initial delay that is not the
                // first of their parameters. EventAI had none and started
                // searching at once, so absent keeps that.
                armed.enabled = true;
                armed.timeMs = armed.rule->Param(4);
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

        // AT ONCE. MoveTargetedHome above has just put the creature into evade
        // mode, and Creature::Update does not call the AI while it is there --
        // so a queued `evaded` sequence waits for a tick that only arrives
        // after the creature is home, by which point JustReachedHome has
        // thrown it away. EventAI ran these in the callback and this is the
        // same instant.
        Fire(RuleId::Evaded, nullptr, nullptr, true);

        m_creature->ResetPlayerDamageReq();
    }

    void MaiCreatureAI::JustReachedHome()
    {
        // Reset FIRST. The other way round -- which is how this read -- the
        // rules were queued and the very next line cleared them, so a
        // `reached_home` rule had never once run a step. Reset here means the
        // steps that follow are the only thing in the queue, and the ones with
        // a time on them survive to be ticked: the creature is out of evade
        // mode by now, so there IS a next UpdateAI.
        Reset();
        Fire(RuleId::ReachedHome, nullptr, nullptr, true);
    }

    void MaiCreatureAI::JustDied(Unit* killer)
    {
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

        // AT ONCE, and BEFORE the reset. Both halves were wrong and each hid
        // the other:
        //
        //   * queued, the sequence waited for an UpdateAI that never comes --
        //     Creature::Update stops calling the AI the moment the creature is
        //     not ALIVE -- so "say this when I die" was silent for every
        //     creature in the world. The only death rules that ever ran were
        //     the handful carrying the random-step flag, which executes in
        //     place;
        //
        //   * and the reset came first, so the phase, the states and the
        //     invincibility a death rule may ask about were already gone. It
        //     is the fight's state, and this is the last moment it is true.
        Fire(RuleId::Died, killer, nullptr, true);

        // Which also puts the phase back to zero. What it no longer throws
        // away is whatever the death rules queued for LATER: Start hands that
        // to the map on its way out, with this creature's state copied into
        // it, because a corpse is never ticked and the map always is. "Say
        // this three seconds after I die" is a creature rule now.
        Reset();
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

            // The school is optional in exactly the same way the spell is, and
            // means the same thing when absent: any. Tested unconditionally it
            // is a mask of zero for every rule that did not name one -- which
            // is every converted row, EventAI writing 0 for "any school" --
            // and nothing overlaps zero, so the rule was skipped at every hit.
            if (armed.rule->Has(1) &&
                !(spell->SchoolMask & armed.rule->Param(1)))
            {
                continue;
            }

            Fire(armed, caster);
        }
    }

    /**
     * My spell landed on somebody.
     *
     * The mirror of SpellHit, and the one trigger EventAI never had: it could
     * hear a spell arrive and had no way to notice one leave. ScriptDev
     * reaches for it whenever a spell must do something PER PERSON HIT and has
     * no script effect to hang it on -- Lethon's Draw Spirit summons a shade
     * for each player it touches, with the author's own note beside it saying
     * the spell has neither a script nor a dummy effect to use instead.
     */
    void MaiCreatureAI::SpellHitTarget(Unit* victim, SpellEntry const* spell)
    {
        if (!spell || !victim)
        {
            return;
        }

        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger != RuleId::SpellHitTarget)
            {
                continue;
            }

            uint32 const wanted = armed.rule->Param(0);
            if (wanted && spell->ID != wanted)
            {
                continue;
            }

            Fire(armed, victim);
        }
    }

    /**
     * One of this creature's auras procced.
     *
     * `aura` is the spell holding it, so a rule names the buff rather than
     * the event. Whether the aura procs at all -- which schools, which spell
     * families, how often, at what chance -- is `spell_proc_event`'s answer
     * and is settled before this is called; what is left for the rule is the
     * part that varies per encounter.
     *
     * @a numbers is snapshotted here rather than read later: the damage that
     * caused this and the amount of the aura are what a step's base points
     * are computed from, and neither survives to a step three seconds in.
     */
    void MaiCreatureAI::AuraProcced(Unit* other, uint32 auraSpellId,
                                    uint32 procSpellId,
                                    Combat::PointsInputs const& numbers)
    {
        for (Armed& armed : m_armed)
        {
            if (armed.rule->trigger != RuleId::AuraProcced)
            {
                continue;
            }

            uint32 const wantedAura = armed.rule->Param(0);
            if (wantedAura && auraSpellId != wantedAura)
            {
                continue;
            }

            uint32 const wantedProc = armed.rule->Param(1);
            if (wantedProc && procSpellId != wantedProc)
            {
                continue;
            }

            Fire(armed, other, nullptr, false, &numbers, procSpellId);
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
        if (m_watchesSight)
        {
            for (Armed& armed : m_armed)
            {
                if (armed.rule->trigger != RuleId::SawUnit)
                {
                    continue;
                }

                // EventAI watched only while out of combat, and every
                // converted row means that. A rule may say otherwise.
                if (m_creature->getVictim() && !armed.rule->Param(6))
                {
                    continue;
                }

                // A rule watches for a friend or for an enemy, never both --
                // unless it says it does not care, which several ScriptDev
                // scripts do by testing nothing at all. Slot 0 is `friendly`,
                // EventAI's own first column; it was named `in_combat` in the
                // manifest, which is the question slot 6 answers.
                if (!armed.rule->Param(5))
                {
                    bool const wantsFriendly = armed.rule->Param(0) != 0;
                    if (wantsFriendly == m_creature->IsHostileTo(who))
                    {
                        continue;
                    }
                }

                // And may name exactly whose arrival it is about. Absent means
                // anyone, which is what every converted row says.
                if (armed.rule->Has(4) &&
                    armed.rule->Param(4) != who->GetEntry())
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

        // Clamped, because the subtraction is between two uint32 and a killing
        // blow is routinely bigger than the health it lands on. Wrapped, the
        // remaining health came out around four billion, every mark was
        // "above", and a creature one-shot from full announced nothing at all
        // -- which is the case where its friends most needed to hear it.
        uint32 const health = m_creature->GetHealth();
        uint32 const left = damage < health ? health - damage : 0;

        float const after = left * 100.0f / m_creature->GetMaxHealth();
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

    /**
     * A quest was taken from, or handed in to, this creature.
     *
     * Not a CreatureAI callback: the world has never had one, and the two
     * triggers were converted, loaded, armed and unreachable -- `Holds` treats
     * them as "it happened" and nothing ever said that it had. The engine
     * calls this from the event the world DOES raise, which is the same moment
     * the quest-start and quest-end sequences run.
     */
    void MaiCreatureAI::QuestFor(Player* player, uint32 questId, bool accepted)
    {
        RuleId const trigger = accepted ? RuleId::QuestAccepted
                                        : RuleId::QuestCompleted;

        for (Armed& armed : m_armed)
        {
            // The quest is not optional on either trigger: a rule that fired
            // on every quest this creature carries is not a thing anybody has
            // ever wanted.
            if (armed.rule->trigger != trigger ||
                armed.rule->Param(0) != questId)
            {
                continue;
            }

            Fire(armed, player);
        }
    }

    // -- Driver: what a step cannot do for itself ----------------------------

    void MaiCreatureAI::SetCombatMovementAllowed(bool enable, bool sendMelee)
    {
        m_actor.combatMovement = enable;

        // The flag, explicitly, because the base class's SetCombatMovement
        // does not touch it in this core -- it only stops or starts the
        // movement that is running now. The flag is the half that LASTS:
        // HandleMovementOnAttackStart reads it at every retarget, so a
        // creature told to stand still and left with the bit set walks up to
        // the next thing it aggroes as if nothing had been said.
        if (enable)
        {
            AddCombatMovementFlags(COMBAT_MOVEMENT_SCRIPT);
        }
        else
        {
            ClearCombatMovementFlags(COMBAT_MOVEMENT_SCRIPT);
        }

        // And now, not at the next decision: the original passes true here
        // unconditionally, because a script that says "stop" and is obeyed
        // three seconds later has not been obeyed. The argument is named at
        // the call because the base signature is two bare bools and the second
        // one gates the ENTIRE body -- `SetCombatMovement(false, false)` is a
        // function call that does nothing at all.
        SetCombatMovement(enable, /* stopOrStartMovement */ true);

        if (!sendMelee || !m_creature->IsInCombat())
        {
            return;
        }

        // What the client is told. Without it a caster ordered to stand off
        // keeps playing its melee swing at a target it is no longer walking
        // to, which is the visible half of the change and the only half a
        // player can see.
        if (Unit* victim = m_creature->getVictim())
        {
            if (enable)
            {
                m_creature->SendMeleeAttackStart(victim);
            }
            else
            {
                m_creature->SendMeleeAttackStop(victim);
            }
        }
    }

    void MaiCreatureAI::SetChase(float distance, float angle)
    {
        // The AI's own pair, not one chase's arguments. A bare MoveChase lasts
        // until the next retarget and no further, and the retarget chases at
        // these two -- so a caster told to keep twenty yards closed to melee
        // the moment its victim changed, which is the fight nobody wrote.
        m_attackDistance = distance;
        m_attackAngle = angle;

        if (!m_actor.combatMovement)
        {
            return;
        }

        if (Unit* victim = m_creature->getVictim())
        {
            m_creature->GetMotionMaster()->MoveChase(victim, m_attackDistance,
                                                     m_attackAngle);
        }
    }

    bool MaiCreatureAI::StartBranch(uint32 kind, uint32 id, ObjectGuid source,
                                    ObjectGuid target)
    {
        Sequence const* sequence = FindSequence(kind, id);
        if (!sequence || sequence->steps.empty())
        {
            return false;
        }

        Frame frame;
        frame.sequence = sequence;
        frame.source = source.IsEmpty() ? m_creature->GetObjectGuid() : source;
        frame.target = target;

        // This one points into the SHARED table, unlike every other frame this
        // creature owns, so it is the one that a reload can pull out from
        // under. Stamped, and checked before it is walked -- see RunFrame.
        frame.stamp = SequenceStamp();

        // Queued, not run: a sequence starting now has had no time pass in it
        // yet, which is the same answer the engine's own frames get.
        m_frames.push_back(frame);
        return true;
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
