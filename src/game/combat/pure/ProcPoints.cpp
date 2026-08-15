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

#include "ProcPoints.h"

#include <cstring>

namespace Combat
{
    namespace
    {
        struct SourceName
        {
            PointsSource source;
            char const*  name;
        };

        const SourceName SOURCE_NAMES[] =
        {
            { PointsSource::None,               "none" },
            { PointsSource::Damage,             "damage" },
            { PointsSource::AuraAmount,         "aura_amount" },
            { PointsSource::ActorMaxHealth,     "actor_max_health" },
            { PointsSource::ActorMaxMana,       "actor_max_mana" },
            { PointsSource::ActorAttackPower,   "actor_attack_power" },
            { PointsSource::TargetCreateHealth, "target_create_health" },
            { PointsSource::ProcSpellManaCost,  "proc_spell_mana_cost" },
            { PointsSource::WeaponDamage,       "weapon_damage" }
        };

        struct ScaleName
        {
            PointsScale scale;
            char const* name;
        };

        const ScaleName SCALE_NAMES[] =
        {
            { PointsScale::Literal,         "literal" },
            { PointsScale::AuraAmount,      "aura_amount" },
            { PointsScale::AuraEffectValue, "aura_effect_value" }
        };
    }

    std::int32_t SourceValue(PointsSource source, PointsInputs const& in)
    {
        switch (source)
        {
            case PointsSource::Damage:             return in.damage;
            case PointsSource::AuraAmount:         return in.auraAmount;
            case PointsSource::ActorMaxHealth:     return in.actorMaxHealth;
            case PointsSource::ActorMaxMana:       return in.actorMaxMana;
            case PointsSource::ActorAttackPower:   return in.actorAttackPower;
            case PointsSource::TargetCreateHealth: return in.targetCreateHealth;
            case PointsSource::ProcSpellManaCost:  return in.procSpellManaCost;
            case PointsSource::WeaponDamage:       return in.weaponDamage;
            default:                               return 0;
        }
    }

    std::int32_t ScaleValue(PointsFormula const& formula,
                            PointsInputs const& in)
    {
        switch (formula.scale)
        {
            case PointsScale::AuraAmount:      return in.auraAmount;
            case PointsScale::AuraEffectValue: return in.auraEffectValue;
            default:                           return formula.coeff;
        }
    }

    std::int32_t EvaluatePoints(PointsFormula const& formula,
                                PointsInputs const& in)
    {
        if (!formula.Defined())
        {
            return 0;
        }

        // Widened for the multiply: attack power against a 300% coefficient
        // overflows a signed 32-bit product on a well-geared character.
        const std::int64_t value = SourceValue(formula.source, in);
        const std::int64_t percent = ScaleValue(formula, in);

        const std::int64_t divisor =
            formula.divisor != 0 ? formula.divisor : 1;

        return static_cast<std::int32_t>(value * percent / 100 / divisor);
    }

    char const* NameOf(PointsSource source)
    {
        for (SourceName const& entry : SOURCE_NAMES)
        {
            if (entry.source == source)
            {
                return entry.name;
            }
        }

        return "none";
    }

    char const* NameOf(PointsScale scale)
    {
        for (ScaleName const& entry : SCALE_NAMES)
        {
            if (entry.scale == scale)
            {
                return entry.name;
            }
        }

        return "literal";
    }

    bool ParsePointsSource(char const* name, PointsSource& out)
    {
        if (!name)
        {
            return false;
        }

        for (SourceName const& entry : SOURCE_NAMES)
        {
            if (std::strcmp(entry.name, name) == 0)
            {
                out = entry.source;
                return true;
            }
        }

        return false;
    }

    bool ParsePointsScale(char const* name, PointsScale& out)
    {
        if (!name)
        {
            return false;
        }

        for (ScaleName const& entry : SCALE_NAMES)
        {
            if (std::strcmp(entry.name, name) == 0)
            {
                out = entry.scale;
                return true;
            }
        }

        return false;
    }
}
