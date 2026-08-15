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

#ifndef MANGOS_COMBAT_MATCHUP_H
#define MANGOS_COMBAT_MATCHUP_H

#include "CombatTypes.h"
#include "Profile.h"

namespace Combat
{
    /**
     * @brief The numbers that only exist for a pair of combatants.
     *
     * Built from two profiles and three facts about the swing. No aura list is
     * walked here and no DBC is read -- everything that needed either already
     * happened when the profiles were built. That is the whole reason a swing
     * can be cheap.
     *
     * All chances are final: skill deltas, expertise and the from-behind and
     * capability gates have already been folded in, and anything the victim
     * cannot do is exactly zero. HitTable adds nothing but a running sum.
     */
    struct Matchup
    {
        Hand hand = Hand::Main;

        Kind attackerKind = Kind::Creature;
        Kind victimKind   = Kind::Creature;

        std::uint8_t  attackerLevel = 1;
        std::uint32_t schoolMask    = SCHOOL_MASK_PHYSICAL;

        Hundredths miss   = 0;

        /// Resist, and the only band the caller fills in rather than Build.
        ///
        /// It depends on the SPELL, not on the two combatants: which mechanic
        /// each effect carries, which school it lands as, whether the victim
        /// happens to hold an aura against that mechanic. None of that is in a
        /// Profile and none of it belongs there -- a Profile describes a unit,
        /// and this describes a unit meeting one particular spell.
        ///
        /// Build leaves it at zero. HitTable::TwoRoll and HitTable::Magic read
        /// it; HitTable::OneRoll never does, because a white swing has no
        /// mechanic to resist.
        Hundredths resist = 0;

        Hundredths dodge  = 0;
        Hundredths parry  = 0;
        Hundredths glance = 0;
        Hundredths block  = 0;
        Hundredths crit   = 0;
        Hundredths crush  = 0;

        /// The victim is evading. Short-circuits the whole table.
        bool evading = false;

        /// The victim is immune to this attacker's melee school. Also a
        /// short-circuit, and checked after evade because an evading creature
        /// is not being fought at all.
        bool immune = false;

        /// A sitting player is hit critically by anything that can crit at
        /// all. Everything between miss and crit is skipped, which is why it
        /// is a flag here rather than a branch inside the roll.
        bool sittingCrit = false;

        float glanceLow  = 1.0f;
        float glanceHigh = 1.0f;

        std::uint32_t armor      = 0;
        std::uint32_t blockValue = 0;

        Hundredths critDamageMod       = 0;
        Hundredths critDamageReduction = 0;

        /**
         * @brief Fold two profiles and a situation into one set of chances.
         *
         * @param attacker  Profile of the unit swinging.
         * @param victim    Profile of the unit being swung at.
         * @param hand      Which weapon.
         * @param situation Geometry and stance, already resolved in the shared
         *                  frame by the caller.
         * @param special   True for a yellow attack: no glancing, no crushing.
         */
        static Matchup Build(Profile const& attacker, Profile const& victim,
                             Hand hand, Situation const& situation,
                             bool special = false);

        /**
         * @brief A matchup for a spell that is resisted rather than avoided.
         *
         * A magic school knows nothing about weapon skill, dodge or parry: the
         * spell either lands or it does not. So this takes the resist chance
         * the caller has already worked out -- from the level gap, the hit
         * auras, the mechanic resistances and the caster's spell hit -- and
         * packages it with the two short circuits every table shares.
         *
         * It is a factory rather than a full Build because there is genuinely
         * nothing to derive: everything that varies is in that one number, and
         * pretending otherwise would mean dragging spell knowledge into a
         * library that must not have any.
         */
        static Matchup Magic(Hundredths resistChance,
                             Situation const& situation,
                             std::uint32_t schoolMask);
    };
}

#endif
