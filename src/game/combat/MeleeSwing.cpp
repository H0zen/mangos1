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

#include "MeleeSwing.h"

#include "CombatRng.h"
#include "CombatShadow.h"
#include "ProfileBuilder.h"
#include "StrikeCommit.h"
#include "combat/pure/CombatConstants.h"
#include "combat/pure/HitTable.h"
#include "combat/pure/Matchup.h"
#include "combat/pure/StrikeResolver.h"

#include "Log.h"
#include "Map.h"
#include "Opcodes.h"
#include "Player.h"
#include "SQLStorages.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldPacket.h"

#include <algorithm>
#include <cstdint>
#include <variant>

namespace Combat
{
    namespace
    {
        /// The C++17 way to give std::visit a set of handlers without writing
        /// a visitor class per variant.
        template <typename... Ts>
        struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };

        template <typename... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        WeaponAttackType LegacyHand(Hand hand)
        {
            return static_cast<WeaponAttackType>(Index(hand));
        }

        /**
         * @brief Apply the damage-bonus auras to a rolled weapon damage.
         *
         * Takes the number that was ROLLED, not the range it was rolled from,
         * and that is the whole point.
         *
         * The first cut bonused the two ends of the range and then rolled
         * uniformly over the result, on the reasoning that these functions are
         * affine and an affine map preserves the distribution. It does not:
         * f(urand(L, H)) and urand(f(L), f(H)) agree only when the slope is
         * exactly one, and they are not even affine to begin with -- both
         * carry floors, caps and per-school clamps. A percentage bonus on a
         * 100-200 weapon turned an integer roll over 101 values into a roll
         * over 151, which is a different weapon.
         *
         * Rolling first and bonusing after is what the old path did, and it is
         * the only order that keeps the weapon's own spread intact.
         *
         * This is still per swing, so the aura walking these two functions do
         * has not gone away yet. Folding them into Profile is what removes it;
         * that needs the dirty-bit plumbing, and it is not this stage.
         */
        std::uint32_t Bonused(Unit& attacker, Unit& victim, Hand hand,
                              std::uint32_t rolled)
        {
            const WeaponAttackType attType = LegacyHand(hand);

            uint32 damage =
                attacker.MeleeDamageBonusDone(&victim, rolled, attType);

            return victim.MeleeDamageBonusTaken(&attacker, damage, attType);
        }

        void RunDamageShield(Unit& shielded, Unit& attacker,
                             DamageShield const& shield)
        {
            SpellEntry const* proto =
                sSpellTemplate.LookupEntry<SpellEntry>(shield.spellId);

            if (!proto)
            {
                return;
            }

            const SpellSchoolMask school = GetSpellSchoolMask(proto);

            uint32 damage = shield.amount;
            damage += attacker.SpellBaseDamageBonusTaken(school);
            shielded.DealDamageMods(&attacker, damage, NULL);

            WorldPacket data(SMSG_SPELLDAMAGESHIELD, (8 + 8 + 4 + 4 + 4));
            data << shielded.GetObjectGuid();
            data << attacker.GetObjectGuid();
            data << uint32(proto->ID);
            data << uint32(damage);
            data << uint32(proto->SchoolMask);
            shielded.SendMessageToSet(&data, true);

            shielded.DealDamage(&attacker, damage, 0, SPELL_DIRECT_DAMAGE,
                                school, proto, true);
        }

        /**
         * @brief The matchup for one white swing.
         *
         * Split out because two callers need it and only one of them swings
         * with it: the legacy path builds it purely to report the bands, which
         * is what makes CombatShadow = 1 a comparison rather than a silence.
         * It rolls nothing and consumes nothing.
         *
         * @param attackerProfile Filled in for the caller, which needs the
         *                        weapon range out of it.
         */
        Matchup MeleeMatchup(Unit& attacker, Unit& victim, Hand hand,
                             Profile const*& attackerProfile)
        {
            // Kept between swings, rebuilt only when the unit said something
            // changed. Both are one unit's own facts; nothing here depends on
            // who is opposite.
            Profile const& mine = attacker.CombatProfile().Read();
            Profile const& theirs = victim.CombatProfile().Read();

            attackerProfile = &mine;

            Situation situation = BuildSituation(&attacker, &victim);
            situation.victimImmune = victim.IsImmuneToDamage(
                SpellSchoolMask(mine.meleeSchoolMask));

            Matchup matchup =
                Matchup::Build(mine, theirs, hand, situation);

            // The one modifier that belongs to neither profile: an aura on
            // the attacker selected by the victim's creature type.
            matchup.critDamageMod += CritDamageVersus(&attacker, &victim);

            return matchup;
        }

        /**
         * @brief One swing through the pre-rewrite engine, in its own order.
         *
         * Damage mods, log, procs, damage -- procs BEFORE the health moves,
         * which is one of the things the rewrite exists to correct, and which
         * this reproduces on purpose. A rollback that quietly fixed things
         * would not be a rollback.
         *
         * The one addition is the guard around the notify: the victim is
         * re-resolved from its guid, because DealMeleeDamage can end it and
         * the original called AttackedBy on the pointer regardless.
         */
        void LegacyStrike(Map& map, Unit& attacker, Unit& victim, Hand hand)
        {
            const ObjectGuid victimGuid = victim.GetObjectGuid();

            CalcDamageInfo info;
            attacker.CalculateMeleeDamage(&victim, &info, LegacyHand(hand));

            attacker.DealDamageMods(&victim, info.damage, &info.absorb);
            attacker.SendAttackStateUpdate(&info);
            attacker.ProcDamageAndSpell(info.target, info.procAttacker,
                                        info.procVictim, info.procEx,
                                        info.damage, info.attackType);
            attacker.DealMeleeDamage(&info, true);

            Unit* survivor = map.GetUnit(victimGuid);
            if (survivor && survivor->IsInWorld())
            {
                survivor->AttackedBy(&attacker);
            }
        }
    }

    void PerformSwing(Unit& attacker, Unit& victim, Hand hand,
                      ReactionQueue& queue, std::uint8_t depth)
    {
        Profile const* attackerProfile = nullptr;

        const Matchup  matchup = MeleeMatchup(attacker, victim, hand,
                                              attackerProfile);
        const HitTable table   = HitTable::OneRoll(matchup);

        // The evidence this rewrite was switched on without. Off by default,
        // and it rolls nothing and consumes nothing when it is on.
        if (ShadowWanted())
        {
            ShadowMeleeChances(attacker, victim, hand, matchup);
        }

        WorldRng rng;

        // Roll the weapon, then bonus what was rolled. The order matters --
        // see Bonused. The resolver is handed the single number as a
        // degenerate range so that the roll happens exactly once, here, over
        // the weapon's own spread.
        DamageRange const& range = attackerProfile->weapon[Index(hand)];

        const std::uint32_t rolled = range.Empty()
            ? 0u
            : rng.RollRange(range.low, std::max(range.low, range.high));

        const std::uint32_t bonused = Bonused(attacker, victim, hand, rolled);
        const DamageRange   weapon{bonused, bonused};

        Strike strike = StrikeResolver::Resolve(matchup, table, weapon, rng);

        if (strike.applied > 0)
        {
            uint32 absorb = 0;
            uint32 resist = 0;

            victim.CalculateDamageAbsorbAndResist(
                &attacker, SpellSchoolMask(strike.schoolMask), DIRECT_DAMAGE,
                strike.applied, &absorb, &resist, true);

            strike.ApplyAbsorbResist(absorb, resist);

            // Sanctuary and the other blanket suppressions. Applied to the
            // committed number, after the log has been decided from it, the
            // same way the old path did.
            uint32 delivered = strike.applied;
            uint32 absorbed  = strike.absorbed;
            attacker.DealDamageMods(&victim, delivered, &absorbed);

            // What DealDamageMods took goes back onto the strike, all of it.
            // Writing only `applied` back left `absorbed` at its pre-call
            // value, so a swing an AI or a sanctuary swallowed whole was
            // logged as a zero with no absorb flag on it, and the clean-damage
            // basis -- rage, skill-up -- never saw the damage either.
            const std::uint32_t swallowed = absorbed - strike.absorbed;

            strike.applied  = delivered;
            strike.absorbed = absorbed;
            strike.clean   += swallowed;
        }
        else if (!strike.finalised)
        {
            strike.ApplyAbsorbResist(0, 0);
        }

        StrikeOrder order;
        order.victim = victim.GetObjectGuid();
        order.strike = strike;
        order.depth  = depth;

        StrikeCommit::Apply(attacker, order, queue);
    }

    void PerformLegacySwing(Unit& attacker, Unit& victim, Hand hand)
    {
        Map* map = attacker.GetMap();
        if (!map)
        {
            return;
        }

        // The report comes first and from the same numbers the new engine
        // would have used, so the log says the same thing in both modes and
        // only the answer that gets APPLIED differs.
        {
            Profile const* attackerProfile = nullptr;

            const Matchup matchup =
                MeleeMatchup(attacker, victim, hand, attackerProfile);

            ShadowMeleeChances(attacker, victim, hand, matchup);
        }

        const ObjectGuid attackerGuid = attacker.GetObjectGuid();
        const ObjectGuid victimGuid   = victim.GetObjectGuid();

        LegacyStrike(*map, attacker, victim, hand);

        // Extra attacks, cashed the way the old path cashed them: main hand,
        // from a counter the procs above may have raised.
        //
        // Two departures from the original loop, and both are things that
        // could only ever have hurt. It is bounded -- `while (m_extraAttacks)`
        // had nothing at all stopping a proc that grants on every swing -- and
        // both ends are looked up again between swings, because a swing can
        // unsummon a pet and the original recursion swung at it anyway.
        for (std::uint32_t i = 0; i < Constants::MAX_EXTRA_ATTACKS; ++i)
        {
            Unit* from = map->GetUnit(attackerGuid);
            Unit* to   = map->GetUnit(victimGuid);

            if (!from || !to || !from->IsInWorld() || !to->IsInWorld() ||
                !from->IsAlive() || !to->IsAlive() || from->m_extraAttacks == 0)
            {
                break;
            }

            --from->m_extraAttacks;

            LegacyStrike(*map, *from, *to, Hand::Main);
        }

        if (Unit* from = map->GetUnit(attackerGuid))
        {
            from->m_extraAttacks = 0;
        }
    }

    void WorldReactionSink::Run(Reaction const& reaction, ReactionQueue& queue)
    {
        // Resolved against the map, not against a unit that happened to be
        // handy. The sink outlives any particular combatant -- the queue is
        // the map's, and by the time a reaction runs the swing that produced
        // it is over.
        Unit* source = m_map.GetUnit(reaction.source);
        Unit* target = m_map.GetUnit(reaction.target);

        // The lifetime handling, in full. Both ends were guids the whole time,
        // so a unit that died or despawned between the swing and here is a
        // failed lookup rather than a freed pointer.
        if (!source || !target || !source->IsInWorld() || !target->IsInWorld())
        {
            return;
        }

        const std::uint8_t next = static_cast<std::uint8_t>(reaction.depth + 1);

        std::visit(Overloaded{
            [&](ExtraSwing const& extra)
            {
                // Both ends are resolved again on EVERY iteration, and this
                // was the one place in the design that did not.
                //
                // A swing can kill. DealDamage promotes a dead creature to a
                // corpse inside the call, and a summoned pet goes further --
                // Unsummon runs CleanupsBeforeDelete immediately, so the unit
                // is stripped of auras and combat state and removed from the
                // world while its memory is still there to be read. Holding
                // Unit& across the first extra and swinging again built a
                // profile from a dismantled unit; a JustDied script that
                // despawns turned it into a plain use-after-free.
                //
                // The queue exists to make that unwritable. Guids in, lookup
                // out, once per swing.
                for (std::uint32_t i = 0; i < extra.count; ++i)
                {
                    Unit* from = m_map.GetUnit(reaction.source);
                    Unit* to   = m_map.GetUnit(reaction.target);

                    if (!from || !to ||
                        !from->IsInWorld() || !to->IsInWorld() ||
                        !from->IsAlive() || !to->IsAlive())
                    {
                        return;
                    }

                    PerformSwing(*from, *to, extra.hand, queue, next);
                }
            },

            [&](ProcTrigger const& proc)
            {
                source->ProcDamageAndSpell(target, proc.attackerMask,
                                           proc.victimMask, proc.extraMask,
                                           proc.damage, LegacyHand(proc.hand));

                // Anything the proc just granted is harvested here, after it
                // ran. The old path read the counter BEFORE the proc and then
                // tested that stale value, so a Windfury granted by this very
                // swing waited for the next one -- and a second grant, Sword
                // Specialization on top of Windfury, was refused outright
                // because the counter was already non-zero.
                //
                // The swings keep the hand that earned them. The old loop
                // always swung main-hand, so an off-hand proc hit with the
                // wrong weapon.
                if (source->m_extraAttacks > 0)
                {
                    Reaction extra;
                    extra.source = reaction.source;
                    extra.target = reaction.target;
                    extra.depth  = next;
                    extra.what   = ExtraSwing::Granted(
                        proc.hand == Hand::Ranged ? Hand::Main : proc.hand,
                        source->m_extraAttacks);

                    source->m_extraAttacks = 0;
                    queue.Push(extra);
                }
            },

            [&](ProcCast const& cast)
            {
                if (!cast.basePoints)
                {
                    source->CastSpell(target, cast.spellId, cast.triggered);
                    return;
                }

                std::int32_t points = *cast.basePoints;
                source->CastCustomSpell(target, cast.spellId, &points,
                                        nullptr, nullptr, cast.triggered);
            },

            [&](DamageShield const& shield)
            {
                // source is the shielded unit; target is who touched it.
                RunDamageShield(*source, *target, shield);
            },

            [&](ItemCombat const& item)
            {
                if (source->GetTypeId() != TYPEID_PLAYER || !target->IsAlive())
                {
                    return;
                }

                static_cast<Player*>(source)->CastItemCombatSpell(
                    target, LegacyHand(item.hand));
            },

            [&](Daze const& daze)
            {
                if (!target->IsAlive())
                {
                    return;
                }

                source->CastSpell(target, daze.spellId, true);
            }
        }, reaction.what);
    }
}
