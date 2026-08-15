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

#ifndef MANGOS_COMBAT_PROFILE_H
#define MANGOS_COMBAT_PROFILE_H

#include "CombatTypes.h"

#include <array>

namespace Combat
{
    /**
     * @brief Everything about one unit that a strike needs, resolved once.
     *
     * This is the answer to the first and largest finding in the audit: every
     * swing re-walked the aura lists, re-read the item bonuses, re-derived the
     * skill values and re-asked the creature type, per hand, per extra attack.
     * None of that changes between two swings unless something changed.
     *
     * A Profile is rebuilt when its owner is stamped dirty and read again --
     * never on the swing path. Engagement compares @ref version against what
     * it saw when it last built a HitTable, and rebuilds only on a mismatch.
     *
     * A unit is both attacker and defender, so both halves live here. Which
     * half is read depends on which side of Matchup::Build it is passed to.
     */
    struct Profile
    {
        /// Bumped on every rebuild. Zero means "never built": Engagement
        /// treats it as always stale, so a missing stamp costs performance and
        /// never correctness.
        std::uint32_t version = 0;

        std::uint8_t level = 1;
        Kind         kind  = Kind::Creature;
        Caps         caps;

        // -- as attacker ---------------------------------------------------

        std::array<DamageRange, HAND_COUNT>   weapon{};
        std::array<std::uint32_t, HAND_COUNT> speedMs{};

        /// Already capped and rating-adjusted by the builder.
        std::array<std::int32_t, HAND_COUNT> weaponSkill{};

        /// 5 * level, the ceiling a weapon skill may contribute.
        std::int32_t maxSkillForLevel = 5;

        /// Own crit, including flat aura modifiers from this side only. The
        /// victim's contribution is @ref critTakenMod on the other profile.
        std::array<Hundredths, HAND_COUNT> critChance{};

        /// Own hit bonus. Reduces miss.
        std::array<Hundredths, HAND_COUNT> hitChance{};

        /// Expertise, already converted to the dodge and parry reduction it
        /// buys. Applied to both, which is correct for 2.4.3.
        std::array<Hundredths, HAND_COUNT> expertiseReduction{};

        /// SPELL_AURA_MOD_COMBAT_RESULT_CHANCE against dodge, from the
        /// attacker's side.
        std::array<Hundredths, HAND_COUNT> dodgeResultMod{};

        /// Crit damage bonus beyond the flat doubling, summed from the
        /// attacker's crit-damage auras and the versus-creature-type ones.
        std::array<Hundredths, HAND_COUNT> critDamageMod{};

        /// The school this unit's melee lands as. Physical for almost
        /// everything; a few creatures swing as fire or nature, and those must
        /// be resisted rather than armoured.
        std::uint32_t meleeSchoolMask = SCHOOL_MASK_PHYSICAL;

        // -- as defender ---------------------------------------------------

        std::int32_t defenseSkill = 5;

        Hundredths dodgeChance = 0;
        Hundredths parryChance = 0;
        Hundredths blockChance = 0;

        std::uint32_t blockValue = 0;
        std::uint32_t armor      = 0;

        /// What this unit does to an incoming attacker's numbers: hit taken
        /// rating and the attacker-hit-chance auras, and the same for crit.
        /// Signs are as the attacker experiences them -- a positive
        /// @ref attackerMissMod makes the attacker miss more.
        Hundredths attackerMissMod = 0;
        Hundredths attackerCritMod = 0;

        /// Resilience, as the fraction of crit damage removed. Carried as a
        /// number so the pure core never has to ask what a Player is.
        Hundredths critDamageReduction = 0;
    };
}

#endif
