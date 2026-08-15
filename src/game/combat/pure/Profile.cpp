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

#include "Profile.h"

namespace Combat
{
    namespace
    {
        /// Compare one per-hand array and name the field if any hand differs.
        template <typename T, std::size_t N>
        bool Same(std::array<T, N> const& a, std::array<T, N> const& b)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                if (!(a[i] == b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool Same(DamageRange const& a, DamageRange const& b)
        {
            return a.low == b.low && a.high == b.high;
        }

        bool Same(std::array<DamageRange, HAND_COUNT> const& a,
                  std::array<DamageRange, HAND_COUNT> const& b)
        {
            for (std::size_t i = 0; i < HAND_COUNT; ++i)
            {
                if (!Same(a[i], b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool Same(Caps const& a, Caps const& b)
        {
            return a.canDodge == b.canDodge &&
                   a.canParry == b.canParry &&
                   a.canBlock == b.canBlock &&
                   a.mayCrush == b.mayCrush &&
                   a.mayGlance == b.mayGlance &&
                   a.dualWielding == b.dualWielding;
        }
    }

    char const* FirstDifference(Profile const& a, Profile const& b)
    {
        // Ordered as the struct is, so a reader can find the field that was
        // named. `version` is deliberately not compared: it is the cache's own
        // bookkeeping and says nothing about what the unit is.

        if (a.level != b.level)                 { return "level"; }
        if (a.kind != b.kind)                   { return "kind"; }
        if (!Same(a.caps, b.caps))              { return "caps"; }

        if (!Same(a.weapon, b.weapon))          { return "weapon"; }
        if (!Same(a.speedMs, b.speedMs))        { return "speedMs"; }

        if (!Same(a.weaponSkill, b.weaponSkill))
        {
            return "weaponSkill";
        }
        if (!Same(a.weaponSkillPvp, b.weaponSkillPvp))
        {
            return "weaponSkillPvp";
        }

        if (a.scalesToOpponent != b.scalesToOpponent)
        {
            return "scalesToOpponent";
        }
        if (a.opponentLevelBonus != b.opponentLevelBonus)
        {
            return "opponentLevelBonus";
        }

        if (!Same(a.critChance, b.critChance))  { return "critChance"; }
        if (!Same(a.hitChance, b.hitChance))    { return "hitChance"; }

        if (!Same(a.expertiseReduction, b.expertiseReduction))
        {
            return "expertiseReduction";
        }
        if (!Same(a.dodgeResultMod, b.dodgeResultMod))
        {
            return "dodgeResultMod";
        }
        if (!Same(a.critDamageMod, b.critDamageMod))
        {
            return "critDamageMod";
        }

        if (a.meleeSchoolMask != b.meleeSchoolMask)
        {
            return "meleeSchoolMask";
        }

        if (a.defenseSkill != b.defenseSkill)   { return "defenseSkill"; }
        if (a.defenseSkillPvp != b.defenseSkillPvp)
        {
            return "defenseSkillPvp";
        }

        if (a.dodgeChance != b.dodgeChance)     { return "dodgeChance"; }
        if (a.parryChance != b.parryChance)     { return "parryChance"; }
        if (a.blockChance != b.blockChance)     { return "blockChance"; }
        if (a.blockValue != b.blockValue)       { return "blockValue"; }
        if (a.armor != b.armor)                 { return "armor"; }

        if (!Same(a.attackerMissMod, b.attackerMissMod))
        {
            return "attackerMissMod";
        }
        if (!Same(a.attackerCritMod, b.attackerCritMod))
        {
            return "attackerCritMod";
        }
        if (!Same(a.attackerCritDamageMod, b.attackerCritDamageMod))
        {
            return "attackerCritDamageMod";
        }

        if (a.critDamageReduction != b.critDamageReduction)
        {
            return "critDamageReduction";
        }

        return nullptr;
    }
}
