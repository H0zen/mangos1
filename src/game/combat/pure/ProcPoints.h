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

#ifndef MANGOS_COMBAT_PROCPOINTS_H
#define MANGOS_COMBAT_PROCPOINTS_H

#include <cstdint>

namespace Combat
{
    /**
     * @brief What a proc's base points are computed FROM.
     *
     * Eight sources cover every triggered spell in the 2.4.3 proc set. The
     * ninth case is an algorithm rather than a formula and is registered by
     * name instead.
     */
    enum class PointsSource : std::uint8_t
    {
        /// The spell's own base points are used; nothing is computed.
        None = 0,

        /// Damage of the event that caused the proc.
        Damage,

        /// The procing aura's own modifier amount.
        AuraAmount,

        ActorMaxHealth,
        ActorMaxMana,
        ActorAttackPower,

        /// The victim's unmodified health, for the percent-of-health procs.
        TargetCreateHealth,

        /// Mana the spell that procced cost to cast.
        ProcSpellManaCost,

        /// The swing's weapon damage, for the melee-proc set.
        WeaponDamage,

        Count
    };

    /// Where the percentage applied to the source comes from.
    enum class PointsScale : std::uint8_t
    {
        /// PointsFormula::coeff, as written in the row.
        Literal = 0,

        /// The aura's amount IS the percentage.
        AuraAmount,

        /// A simple value off one of the aura spell's effects.
        AuraEffectValue,

        Count
    };

    /**
     * @brief Everything a formula may read, gathered once by the caller.
     *
     * A plain bag of integers so the evaluation stays pure: no unit, no spell,
     * nothing to look up. The caller fills only what the formula it is about
     * to run actually needs; the rest stay zero.
     */
    struct PointsInputs
    {
        std::int32_t damage             = 0;
        std::int32_t auraAmount         = 0;
        std::int32_t auraEffectValue    = 0;
        std::int32_t actorMaxHealth     = 0;
        std::int32_t actorMaxMana       = 0;
        std::int32_t actorAttackPower   = 0;
        std::int32_t targetCreateHealth = 0;
        std::int32_t procSpellManaCost  = 0;
        std::int32_t weaponDamage       = 0;
    };

    /**
     * @brief One proc's base-points formula, as a value.
     *
     * `points = source * percent / 100 / divisor`
     *
     * That single shape covers thirty-four of the thirty-eight formulas in the
     * proc set. The remaining four carry their own arithmetic and are named
     * behaviours, not rows.
     */
    struct PointsFormula
    {
        PointsSource source = PointsSource::None;
        PointsScale  scale  = PointsScale::Literal;

        /// Percent, used when @ref scale is Literal. 100 means "as is".
        std::int32_t coeff = 100;

        /// Divides the result, for the procs that spread over ticks. Never
        /// zero; a zero is treated as one.
        std::int32_t divisor = 1;

        bool Defined() const
        {
            return source != PointsSource::None;
        }
    };

    /// Read the source out of @p in.
    std::int32_t SourceValue(PointsSource source, PointsInputs const& in);

    /// The percentage the formula applies.
    std::int32_t ScaleValue(PointsFormula const& formula,
                            PointsInputs const& in);

    /**
     * @brief Evaluate the formula.
     *
     * @return the base points, or zero when the formula is not defined.
     */
    std::int32_t EvaluatePoints(PointsFormula const& formula,
                                PointsInputs const& in);

    /// Names, for the SQL loader and the error messages.
    char const* NameOf(PointsSource source);
    char const* NameOf(PointsScale scale);

    /// Parse a column value. Returns false on an unknown name.
    bool ParsePointsSource(char const* name, PointsSource& out);
    bool ParsePointsScale(char const* name, PointsScale& out);
}

#endif
