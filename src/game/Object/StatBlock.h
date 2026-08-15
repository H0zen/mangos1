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

#ifndef MANGOS_H_STATBLOCK
#define MANGOS_H_STATBLOCK

#include <cstddef>

/**
 * @brief Every number a unit's stats are assembled from, and the assembly.
 *
 * Held by value on Unit. Depends on nothing -- not on Unit, not on the world,
 * not on a DBC -- so the arithmetic below can be asserted on a bare struct
 * with written-down numbers.
 *
 * The two enums live here rather than in Unit.h because they index this and
 * nothing else.
 */

/// Which of the four slots a modifier occupies. Their meanings are not
/// interchangeable: two are added and two multiply, and the order they are
/// combined in is the whole of StatBlock::Combine.
enum UnitModifierType
{
    BASE_VALUE = 0,
    BASE_PCT = 1,
    TOTAL_VALUE = 2,
    TOTAL_PCT = 3,
    MODIFIER_TYPE_END = 4
};

enum UnitMods
{
    UNIT_MOD_STAT_STRENGTH,                                 // UNIT_MOD_STAT_STRENGTH..UNIT_MOD_STAT_SPIRIT must be in existing order, it's accessed by index values of Stats enum.
    UNIT_MOD_STAT_AGILITY,
    UNIT_MOD_STAT_STAMINA,
    UNIT_MOD_STAT_INTELLECT,
    UNIT_MOD_STAT_SPIRIT,
    UNIT_MOD_HEALTH,
    UNIT_MOD_MANA,                                          // UNIT_MOD_MANA..UNIT_MOD_HAPPINESS must be in existing order, it's accessed by index values of Powers enum.
    UNIT_MOD_RAGE,
    UNIT_MOD_FOCUS,
    UNIT_MOD_ENERGY,
    UNIT_MOD_HAPPINESS,
    UNIT_MOD_ARMOR,                                         // UNIT_MOD_ARMOR..UNIT_MOD_RESISTANCE_ARCANE must be in existing order, it's accessed by index values of SpellSchools enum.
    UNIT_MOD_RESISTANCE_HOLY,
    UNIT_MOD_RESISTANCE_FIRE,
    UNIT_MOD_RESISTANCE_NATURE,
    UNIT_MOD_RESISTANCE_FROST,
    UNIT_MOD_RESISTANCE_SHADOW,
    UNIT_MOD_RESISTANCE_ARCANE,
    UNIT_MOD_ATTACK_POWER,
    UNIT_MOD_ATTACK_POWER_RANGED,
    UNIT_MOD_DAMAGE_MAINHAND,
    UNIT_MOD_DAMAGE_OFFHAND,
    UNIT_MOD_DAMAGE_RANGED,
    UNIT_MOD_END,
    // synonyms
    UNIT_MOD_STAT_START = UNIT_MOD_STAT_STRENGTH,
    UNIT_MOD_STAT_END = UNIT_MOD_STAT_SPIRIT + 1,
    UNIT_MOD_RESISTANCE_START = UNIT_MOD_ARMOR,
    UNIT_MOD_RESISTANCE_END = UNIT_MOD_RESISTANCE_ARCANE + 1,
    UNIT_MOD_POWER_START = UNIT_MOD_MANA,
    UNIT_MOD_POWER_END = UNIT_MOD_HAPPINESS + 1
};

/**
 * @brief The four modifier slots per stat group, and how they combine.
 *
 * A percentage slot holds a MULTIPLIER, not a percentage: 1.0 is "unchanged".
 * That is why the block starts full of ones in two of its four columns and
 * zeroes in the other two, and why applying a percentage multiplies rather
 * than adds.
 */
class StatBlock
{
    public:
        StatBlock()
        {
            Reset();
        }

        /// Every value back to its neutral element: zero for the two that are
        /// added, one for the two that multiply.
        void Reset();

        /// True when both indices are in range. Everything below assumes it.
        static bool InRange(UnitMods unitMod, UnitModifierType type)
        {
            return unitMod < UNIT_MOD_END && type < MODIFIER_TYPE_END;
        }

        float Raw(UnitMods unitMod, UnitModifierType type) const
        {
            return m_values[unitMod][type];
        }

        void Set(UnitMods unitMod, UnitModifierType type, float value)
        {
            m_values[unitMod][type] = value;
        }

        /**
         * @brief The stored value, with the one reading that is not raw.
         *
         * A TOTAL_PCT at or below zero means the stat has been zeroed out
         * rather than scaled, and reports as zero so that a caller multiplying
         * by it cannot turn a negative multiplier into a positive result.
         */
        float Value(UnitMods unitMod, UnitModifierType type) const;

        /**
         * @brief Add or remove one modifier.
         *
         * The two value slots add and subtract; the two percentage slots
         * multiply and divide by (100 + amount) / 100. A modifier of -100% or
         * worse is clamped to -200% before the division, which is what keeps
         * removing it from dividing by zero.
         *
         * @return false when the indices are out of range and nothing was
         *         changed.
         */
        bool Apply(UnitMods unitMod, UnitModifierType type, float amount,
                   bool apply);

        /**
         * @brief The assembled value of a group.
         *
         *     ((base + createValue) * base_pct + total) * total_pct
         *
         * @param createValue the unmodified value the unit was made with,
         *        which the block does not know and the caller does.
         *
         * @return zero when TOTAL_PCT is at or below zero: the stat is off,
         *         not negative.
         */
        float Combine(UnitMods unitMod, float createValue) const;

    private:
        float m_values[UNIT_MOD_END][MODIFIER_TYPE_END];
};

#endif
