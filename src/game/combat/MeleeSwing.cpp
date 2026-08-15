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
#include "ProfileBuilder.h"
#include "StrikeCommit.h"
#include "combat/pure/HitTable.h"
#include "combat/pure/Matchup.h"
#include "combat/pure/StrikeResolver.h"

#include "Log.h"
#include "ObjectLookup.h"
#include "Opcodes.h"
#include "Player.h"
#include "SQLStorages.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldPacket.h"

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
         * @brief Apply the damage-bonus auras to the weapon range.
         *
         * The old path rolled a damage value and then ran it through
         * MeleeDamageBonusDone and MeleeDamageBonusTaken. Both are affine in
         * the damage -- a flat term and a percentage -- so applying them to
         * the two ends of the range gives the same distribution as applying
         * them to the roll, and the roll stays an integer roll over an integer
         * range.
         *
         * This is still per swing, so the aura walking these two functions do
         * has not gone away yet. Folding them into Profile is what removes it;
         * that needs the dirty-bit plumbing, and it is not this stage.
         */
        DamageRange Bonused(Unit& attacker, Unit& victim, Hand hand,
                            DamageRange range)
        {
            const WeaponAttackType attType = LegacyHand(hand);

            range.low = attacker.MeleeDamageBonusDone(&victim, range.low, attType);
            range.low = victim.MeleeDamageBonusTaken(&attacker, range.low, attType);

            range.high = attacker.MeleeDamageBonusDone(&victim, range.high, attType);
            range.high = victim.MeleeDamageBonusTaken(&attacker, range.high, attType);

            if (range.high < range.low)
            {
                range.high = range.low;
            }

            return range;
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
    }

    void PerformSwing(Unit& attacker, Unit& victim, Hand hand,
                      ReactionQueue& queue, std::uint8_t depth)
    {
        const Profile attackerProfile = BuildProfile(&attacker, &victim);
        const Profile victimProfile   = BuildProfile(&victim, &attacker);

        Situation situation = BuildSituation(&attacker, &victim);
        situation.victimImmune = victim.IsImmuneToDamage(
            SpellSchoolMask(attackerProfile.meleeSchoolMask));

        const Matchup  matchup = Matchup::Build(attackerProfile, victimProfile,
                                                hand, situation);
        const HitTable table   = HitTable::OneRoll(matchup);

        const DamageRange weapon = Bonused(
            attacker, victim, hand, attackerProfile.weapon[Index(hand)]);

        WorldRng rng;
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
            strike.applied = delivered;
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

    void WorldReactionSink::Run(Reaction const& reaction, ReactionQueue& queue)
    {
        Unit* source = ObjectLookup::GetUnit(m_anchor, reaction.source);
        Unit* target = ObjectLookup::GetUnit(m_anchor, reaction.target);

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
                if (!source->IsAlive() || !target->IsAlive())
                {
                    return;
                }

                for (std::uint32_t i = 0; i < extra.count; ++i)
                {
                    PerformSwing(*source, *target, extra.hand, queue, next);
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
                    extra.what   = ExtraSwing{
                        proc.hand == Hand::Ranged ? Hand::Main : proc.hand,
                        source->m_extraAttacks};

                    source->m_extraAttacks = 0;
                    queue.Push(extra);
                }
            },

            [&](ProcCast const& cast)
            {
                source->CastSpell(target, cast.spellId, cast.triggered);
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
