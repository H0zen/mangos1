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

#include "StrikeCommit.h"

#include "Creature.h"
#include "ObjectLookup.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Utilities/MathDefines.h"

namespace Combat
{
    namespace
    {
        MeleeHitOutcome LegacyOutcome(Outcome outcome)
        {
            switch (outcome)
            {
                case Outcome::Evade:
                case Outcome::Immune:   return MELEE_HIT_EVADE;
                case Outcome::Miss:     return MELEE_HIT_MISS;
                case Outcome::Dodge:    return MELEE_HIT_DODGE;
                case Outcome::Parry:    return MELEE_HIT_PARRY;
                case Outcome::Glancing: return MELEE_HIT_GLANCING;
                case Outcome::Block:    return MELEE_HIT_BLOCK;
                case Outcome::Crit:     return MELEE_HIT_CRIT;
                case Outcome::Crushing: return MELEE_HIT_CRUSHING;
                default:                return MELEE_HIT_NORMAL;
            }
        }

        WeaponAttackType LegacyHand(Hand hand)
        {
            return static_cast<WeaponAttackType>(Index(hand));
        }

        /// The presentation half of a strike: what the client is told and what
        /// the proc system keys off. Derived from the outcome rather than
        /// carried through the pure core, which has no business knowing an
        /// opcode's flag layout.
        void Present(Strike const& strike, CalcDamageInfo& info)
        {
            switch (strike.hand)
            {
                case Hand::Off:
                    info.procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_HIT |
                                        PROC_FLAG_SUCCESSFUL_OFFHAND_HIT;
                    info.procVictim   = PROC_FLAG_TAKEN_MELEE_HIT;
                    info.HitInfo      = HITINFO_LEFTSWING;
                    break;

                case Hand::Ranged:
                    info.procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
                    info.procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
                    info.HitInfo      = HITINFO_UNK3;
                    break;

                default:
                    info.procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_HIT;
                    info.procVictim   = PROC_FLAG_TAKEN_MELEE_HIT;
                    info.HitInfo      = HITINFO_NORMALSWING2;
                    break;
            }

            switch (strike.outcome)
            {
                case Outcome::Evade:
                    info.HitInfo    |= HITINFO_MISS | HITINFO_SWINGNOHITSOUND;
                    info.TargetState = VICTIMSTATE_EVADES;
                    info.procEx     |= PROC_EX_EVADE;
                    break;

                case Outcome::Immune:
                    info.HitInfo    |= HITINFO_NORMALSWING;
                    info.TargetState = VICTIMSTATE_IS_IMMUNE;
                    info.procEx     |= PROC_EX_IMMUNE;
                    break;

                case Outcome::Miss:
                    info.HitInfo    |= HITINFO_MISS;
                    info.TargetState = VICTIMSTATE_UNAFFECTED;
                    info.procEx     |= PROC_EX_MISS;
                    break;

                case Outcome::Dodge:
                    info.TargetState = VICTIMSTATE_DODGE;
                    info.procEx     |= PROC_EX_DODGE;
                    break;

                case Outcome::Parry:
                    info.TargetState = VICTIMSTATE_PARRY;
                    info.procEx     |= PROC_EX_PARRY;
                    break;

                case Outcome::Crit:
                    info.HitInfo    |= HITINFO_CRITICALHIT;
                    info.TargetState = VICTIMSTATE_NORMAL;
                    info.procEx     |= PROC_EX_CRITICAL_HIT;
                    break;

                case Outcome::Glancing:
                    info.HitInfo    |= HITINFO_GLANCING;
                    info.TargetState = VICTIMSTATE_NORMAL;
                    info.procEx     |= PROC_EX_NORMAL_HIT;
                    break;

                case Outcome::Crushing:
                    info.HitInfo    |= HITINFO_CRUSHING;
                    info.TargetState = VICTIMSTATE_NORMAL;
                    info.procEx     |= PROC_EX_NORMAL_HIT;
                    break;

                case Outcome::Block:
                    // A block that swallowed everything reads differently to
                    // the client than one the swing came through.
                    info.TargetState = strike.applied == 0
                        ? VICTIMSTATE_BLOCKS
                        : VICTIMSTATE_NORMAL;
                    info.procEx |= PROC_EX_BLOCK;
                    if (strike.applied > 0)
                    {
                        info.procEx |= PROC_EX_NORMAL_HIT;
                    }
                    break;

                default:
                    info.TargetState = VICTIMSTATE_NORMAL;
                    info.procEx     |= PROC_EX_NORMAL_HIT;
                    break;
            }

            if (strike.absorbed > 0)
            {
                info.HitInfo |= HITINFO_ABSORB;
                info.procEx  |= PROC_EX_ABSORB;
            }
            if (strike.resisted > 0)
            {
                info.HitInfo |= HITINFO_RESIST;
            }
            if (strike.applied > 0)
            {
                info.procVictim |= PROC_FLAG_TAKEN_ANY_DAMAGE;
            }
        }

        /// A parried swing hurries the parrier's own next one along. The form
        /// is 2.4.3; the units are not -- the timer read here is hasted and
        /// the thresholds are computed from an unhasted attack time, so Flurry
        /// bends the bands. Carried over unchanged: stage 6 owns the numbers.
        void HastenAfterParry(Unit& victim)
        {
            if (victim.GetTypeId() == TYPEID_UNIT)
            {
                CreatureInfo const* info =
                    static_cast<Creature&>(victim).GetCreatureInfo();

                if (info && (info->ExtraFlags & CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN))
                {
                    return;
                }
            }

            const bool useOffhand = victim.haveOffhandWeapon() &&
                victim.getAttackTimer(OFF_ATTACK) < victim.getAttackTimer(BASE_ATTACK);

            const WeaponAttackType hand = useOffhand ? OFF_ATTACK : BASE_ATTACK;

            float remaining = float(victim.getAttackTimer(hand));
            const float percent20 = victim.GetAttackTime(hand) * 0.20f;
            const float percent60 = 3.0f * percent20;

            if (remaining > percent20 && remaining <= percent60)
            {
                victim.setAttackTimer(hand, uint32(percent20));
            }
            else if (remaining > percent60)
            {
                remaining -= 2.0f * percent20;
                victim.setAttackTimer(hand, uint32(remaining));
            }
        }

        /// Snapshot the victim's damage shields into the queue.
        ///
        /// The old path walked the live aura list while dealing damage that
        /// could remove an aura from it -- and reset the iterator to begin()
        /// on every hit, so the guard against running one twice was a set of
        /// raw Aura pointers that the same damage could free. Reading the
        /// amounts once, here, is what makes that unwritable.
        void QueueDamageShields(Unit& attacker, Unit& victim,
                                ReactionQueue& queue, std::uint8_t depth)
        {
            Unit::AuraList const& shields =
                victim.GetAurasByType(SPELL_AURA_DAMAGE_SHIELD);

            for (Unit::AuraList::const_iterator it = shields.begin();
                 it != shields.end(); ++it)
            {
                Aura* aura = *it;
                if (!aura)
                {
                    continue;
                }

                SpellEntry const* proto = aura->GetSpellProto();
                if (!proto)
                {
                    continue;
                }

                Reaction reaction;
                reaction.source = victim.GetObjectGuid();
                reaction.target = attacker.GetObjectGuid();
                reaction.depth  = depth;
                reaction.what   = DamageShield{
                    proto->ID,
                    uint32(aura->GetModifier()->m_amount),
                    uint32(proto->SchoolMask)};

                queue.Push(reaction);
            }
        }

        /// A creature striking from behind may daze. Conditions unchanged.
        bool DazeApplies(Unit const& attacker, Unit const& victim,
                         Strike const& strike)
        {
            if (attacker.GetTypeId() == TYPEID_PLAYER)
            {
                return false;
            }

            if (static_cast<Creature const&>(attacker).GetCharmerOrOwnerGuid())
            {
                return false;
            }

            switch (strike.outcome)
            {
                case Outcome::Crit:
                case Outcome::Crushing:
                case Outcome::Normal:
                case Outcome::Glancing:
                    break;
                default:
                    return false;
            }

            return !victim.Where().HasInArc(attacker.Where(), M_PI_F);
        }
    }

    CommitResult StrikeCommit::Apply(Unit& attacker, StrikeOrder const& order,
                                     ReactionQueue& queue)
    {
        CommitResult result;

        // -- 1. Resolve ----------------------------------------------------

        Unit* victim = ObjectLookup::GetUnit(attacker, order.victim);
        if (!victim || !victim->IsInWorld())
        {
            queue.DropInvolving(order.victim);
            return result;
        }

        result.resolved = true;

        Strike const& strike = order.strike;

        CalcDamageInfo info;
        info.attacker         = &attacker;
        info.target           = victim;
        info.damageSchoolMask = SpellSchoolMask(strike.schoolMask);
        info.attackType       = LegacyHand(strike.hand);
        info.damage           = strike.applied;
        info.cleanDamage      = strike.clean;
        info.absorb           = strike.absorbed;
        info.resist           = strike.resisted;
        info.blocked_amount   = strike.blocked;
        info.hitOutCome       = LegacyOutcome(strike.outcome);
        info.HitInfo          = HITINFO_NORMALSWING;
        info.TargetState      = VICTIMSTATE_UNAFFECTED;
        info.procAttacker     = PROC_FLAG_NONE;
        info.procVictim       = PROC_FLAG_NONE;
        info.procEx           = PROC_EX_NONE;

        Present(strike, info);

        // -- 2. Log --------------------------------------------------------

        // Sent before the health moves, and truthful because the numbers were
        // all decided before anything ran. The old path sent this, then let
        // the proc machinery run, then applied damage -- so a proc that killed
        // the target left a log entry describing damage that never landed.
        attacker.SendAttackStateUpdate(&info);

        // -- 3. Health -----------------------------------------------------

        const bool deliverable =
            victim->IsAlive() && !victim->IsTaxiFlying() &&
            !(victim->GetTypeId() == TYPEID_UNIT &&
              static_cast<Creature*>(victim)->IsInEvadeMode()) &&
            attacker.IsAllowedDamageInArea(victim);

        if (deliverable)
        {
            if (strike.outcome == Outcome::Crit)
            {
                victim->HandleEmoteCommand(EMOTE_ONESHOT_WOUNDCRITICAL);
            }
            if (strike.blocked > 0 && info.TargetState != VICTIMSTATE_BLOCKS)
            {
                victim->HandleEmoteCommand(EMOTE_ONESHOT_PARRYSHIELD);
            }
            if (strike.outcome == Outcome::Parry)
            {
                HastenAfterParry(*victim);
            }

            // -- 4. Threat lives inside this call, along with rage, the AI
            // notification and the kill. Taking it apart is a later stage.
            CleanDamage clean(strike.clean, info.attackType, info.hitOutCome);
            attacker.DealDamage(victim, strike.applied, &clean, DIRECT_DAMAGE,
                                info.damageSchoolMask, NULL, true);

            result.applied = strike.applied > 0;
        }

        // -- 5. Death ------------------------------------------------------

        if (!victim->IsAlive())
        {
            result.victimDied = true;

            // Everything still queued against this unit stops existing. No
            // lifetime tracking, no null checks scattered down the chain --
            // the reaction is simply not there any more.
            queue.DropInvolving(order.victim);
            return result;
        }

        // -- 6. React ------------------------------------------------------

        const ObjectGuid attackerGuid = attacker.GetObjectGuid();
        const ObjectGuid victimGuid   = victim->GetObjectGuid();

        Reaction procs;
        procs.source = attackerGuid;
        procs.target = victimGuid;
        procs.depth  = order.depth;
        procs.what   = ProcTrigger{info.procAttacker, info.procVictim,
                                   info.procEx, strike.applied, strike.hand};
        queue.Push(procs);

        if (strike.outcome != Outcome::Miss && strike.outcome != Outcome::Evade)
        {
            if (attacker.GetTypeId() == TYPEID_PLAYER)
            {
                Reaction weapon;
                weapon.source = attackerGuid;
                weapon.target = victimGuid;
                weapon.depth  = order.depth;
                weapon.what   = ItemCombat{strike.hand};
                queue.Push(weapon);
            }

            QueueDamageShields(attacker, *victim, queue, order.depth);
        }

        if (result.applied && DazeApplies(attacker, *victim, strike))
        {
            Reaction daze;
            daze.source = attackerGuid;
            daze.target = victimGuid;
            daze.depth  = order.depth;
            daze.what   = Daze{};
            queue.Push(daze);
        }

        // -- 7. Notify -----------------------------------------------------

        // Once. DealDamage already told the AI it was attacked; the old path
        // then called AttackedBy again from AttackerStateUpdate, so every
        // white hit notified twice.
        if (!deliverable)
        {
            victim->AttackedBy(&attacker);
        }

        return result;
    }
}
