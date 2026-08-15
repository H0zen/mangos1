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

#include "StrikeResolver.h"

#include "CombatConstants.h"

#include <algorithm>

namespace Combat
{
    void Strike::ApplyAbsorbResist(std::uint32_t absorb, std::uint32_t resist)
    {
        const std::uint32_t standing = applied;

        absorbed = std::min(absorb, standing);
        resisted = std::min(resist, standing - absorbed);

        applied = standing - absorbed - resisted;
        clean += absorbed + resisted;
        finalised = true;
    }

    namespace
    {
        using namespace Constants;

        /// Scale by a percentage expressed in hundredths, rounding to nearest.
        std::uint32_t ScaleByHundredths(std::uint32_t value, Hundredths pct)
        {
            if (pct == 0)
            {
                return value;
            }

            const float scaled = static_cast<float>(value) *
                (1.0f + static_cast<float>(pct) /
                        static_cast<float>(HUNDRED_PERCENT));

            return scaled <= 0.0f
                ? 0u
                : static_cast<std::uint32_t>(scaled + 0.5f);
        }

        /// The outcome multiplier. Applied to the raw weapon roll, before
        /// armour -- which is the correction this resolver exists for.
        std::uint32_t ApplyOutcome(Outcome outcome, std::uint32_t raw,
                                   Matchup const& m, Rng& rng,
                                   std::uint32_t& intoClean)
        {
            switch (outcome)
            {
                case Outcome::Crit:
                {
                    std::uint32_t damage = raw * CRIT_MULTIPLIER_NUMERATOR;
                    damage = ScaleByHundredths(damage, m.critDamageMod);

                    // Resilience takes its cut of the crit, and what it takes
                    // is rage for the victim rather than nothing at all.
                    if (m.critDamageReduction > 0)
                    {
                        const std::uint32_t kept =
                            ScaleByHundredths(damage, -m.critDamageReduction);
                        intoClean += damage - kept;
                        damage = kept;
                    }
                    return damage;
                }

                case Outcome::Crushing:
                    return raw * CRUSH_NUMERATOR / CRUSH_DENOMINATOR;

                case Outcome::Glancing:
                {
                    const float factor = m.glanceLow +
                        rng.RollUnit() * (m.glanceHigh - m.glanceLow);

                    const std::uint32_t reduced = static_cast<std::uint32_t>(
                        static_cast<float>(raw) * factor);

                    intoClean += raw - std::min(reduced, raw);
                    return reduced;
                }

                default:
                    return raw;
            }
        }
    }

    float StrikeResolver::ArmourSurvival(std::uint32_t armor,
                                         std::uint8_t attackerLevel)
    {
        if (armor == 0)
        {
            return 1.0f;
        }

        float levelMod = static_cast<float>(attackerLevel);
        if (levelMod > ARMOUR_STEEP_LEVEL)
        {
            levelMod += ARMOUR_STEEP_SLOPE * (levelMod - ARMOUR_STEEP_LEVEL);
        }

        const float value = static_cast<float>(armor);
        const float denominator =
            value + ARMOUR_CONSTANT + ARMOUR_LEVEL_FACTOR * levelMod;

        if (denominator <= 0.0f)
        {
            return 1.0f;
        }

        const float reduction =
            std::min(value / denominator, ARMOUR_MAX_REDUCTION);

        return 1.0f - std::max(reduction, 0.0f);
    }

    Strike StrikeResolver::Resolve(Matchup const& m, HitTable const& table,
                                   DamageRange const& weapon, Rng& rng)
    {
        Strike s;
        s.hand       = m.hand;
        s.schoolMask = m.schoolMask;
        s.outcome    = table.Resolve(rng.Roll10000());

        // An evading victim is not swung at, so nothing is rolled: no damage,
        // no rage basis, no skill-up.
        if (s.outcome == Outcome::Evade)
        {
            s.finalised = true;
            return s;
        }

        s.raw = weapon.Empty()
            ? 0u
            : rng.RollRange(weapon.low, std::max(weapon.low, weapon.high));

        // A miss rolls nothing further. The victim did not take the swing and
        // did not answer it either, so there is no clean damage.
        if (s.outcome == Outcome::Miss)
        {
            s.afterRoll  = 0;
            s.afterArmor = 0;
            s.finalised  = true;
            return s;
        }

        s.afterRoll = ApplyOutcome(s.outcome, s.raw, m, rng, s.clean);

        // Armour is a physical mitigation. Melee that lands as fire or nature
        // is resisted instead, and the resolver must not do both.
        if (IsPhysical(s.schoolMask) && s.afterRoll > 0)
        {
            const float survival = ArmourSurvival(m.armor, m.attackerLevel);
            const std::uint32_t survived = std::max(
                static_cast<std::uint32_t>(
                    static_cast<float>(s.afterRoll) * survival),
                MINIMUM_APPLIED_DAMAGE);

            s.afterArmor = std::min(survived, s.afterRoll);
            s.clean += s.afterRoll - s.afterArmor;
        }
        else
        {
            s.afterArmor = s.afterRoll;
        }

        // Dodge and parry: the swing was answered. It carries no damage, but
        // what it would have carried is the rage basis for both sides.
        if (s.outcome == Outcome::Dodge || s.outcome == Outcome::Parry)
        {
            s.clean += s.afterArmor;
            s.applied = 0;
            s.finalised = true;
            return s;
        }

        std::uint32_t standing = s.afterArmor;

        // The block is a flat value and it comes after armour.
        if (s.outcome == Outcome::Block)
        {
            s.blocked = std::min(m.blockValue, standing);
            standing -= s.blocked;
            s.clean += s.blocked;
        }

        s.applied = standing;
        return s;
    }
}
