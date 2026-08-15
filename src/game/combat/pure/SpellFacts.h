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

#ifndef MANGOS_COMBAT_SPELLFACTS_H
#define MANGOS_COMBAT_SPELLFACTS_H

#include "CombatTypes.h"

#include <array>

namespace Combat
{
    /// A TBC spell has at most three effects. That is the DBC limit, not a
    /// choice, so the array is fixed and there is nothing to allocate.
    constexpr std::size_t MAX_SPELL_EFFECTS = 3;

    /**
     * @brief One effect, decoded.
     *
     * The DBC row keeps radius as an INDEX into another store, so every
     * question about how far a spell reaches costs a second lookup. Here it is
     * a number, resolved once.
     */
    struct SpellEffectFacts
    {
        std::uint32_t effect      = 0;   ///< SPELL_EFFECT_*
        std::uint32_t auraType    = 0;   ///< SPELL_AURA_*, 0 when not an aura
        std::int32_t  basePoints  = 0;
        std::int32_t  dieSides    = 0;
        std::uint32_t targetA     = 0;
        std::uint32_t targetB     = 0;
        std::uint32_t mechanic    = 0;
        std::uint32_t triggerSpell = 0;
        std::int32_t  amplitudeMs = 0;

        /// Resolved from RadiusIndex, in yards. Zero means not an area effect.
        float radius = 0.0f;

        bool present  = false;
        bool positive = false;
    };

    /**
     * @brief Everything about a spell that does not depend on the world.
     *
     * Filled once at start-up and never changed, so it can be const and
     * shared. This is the answer to the first two findings of the spell
     * audit: the engine re-reads the DBC row on every question, and asks the
     * same questions by ID while already holding the row.
     *
     * Pure by the same rule as the rest of this directory -- no DBC type
     * appears here, only the values read out of one. The loader lives in
     * src/game/combat and is the only file that knows what a SpellEntry is.
     */
    struct SpellFacts
    {
        std::uint32_t id = 0;

        std::uint32_t family      = 0;
        std::uint64_t familyFlags = 0;
        std::uint32_t schoolMask  = 0;
        std::uint32_t dispelType  = 0;
        std::uint32_t mechanic    = 0;

        /// Union of the spell mechanic and every effect mechanic.
        ///
        /// Computed with a guard on zero. The live code writes
        /// 1 << (mechanic - 1) in two places without one, which is undefined
        /// when a spell has no mechanic at all.
        std::uint32_t mechanicMask = 0;

        /// Resolved, not an index into sSpellDurationStore.
        std::int32_t durationMs    = 0;
        std::int32_t maxDurationMs = 0;

        /// Resolved, not an index into sSpellCastTimesStore. Before any
        /// caster modifier: those are per-cast and stay where they are.
        std::uint32_t castTimeMs = 0;

        /// Resolved, not an index into sSpellRangeStore.
        float rangeMin = 0.0f;
        float rangeMax = 0.0f;

        std::uint32_t recoveryTimeMs = 0;

        std::array<SpellEffectFacts, MAX_SPELL_EFFECTS> effects{};
        std::uint8_t effectCount = 0;

        bool channeled       = false;
        bool passive         = false;
        bool areaOfEffect    = false;
        bool breaksStealth   = false;
        bool deathPersistent = false;

        /// True when the row exists at all. A fact for a spell the DBC does
        /// not have is all zeroes and this is false, so a caller that forgets
        /// to check gets nothing rather than garbage.
        bool known = false;

        SpellEffectFacts const& Effect(std::size_t index) const
        {
            return effects[index < MAX_SPELL_EFFECTS ? index : 0];
        }

        bool HasEffect(std::uint32_t effectId) const
        {
            for (SpellEffectFacts const& effect : effects)
            {
                if (effect.present && effect.effect == effectId)
                {
                    return true;
                }
            }
            return false;
        }

        bool HasAura(std::uint32_t auraId) const
        {
            for (SpellEffectFacts const& effect : effects)
            {
                if (effect.present && effect.auraType == auraId)
                {
                    return true;
                }
            }
            return false;
        }
    };

    /// The mechanic mask bit for a mechanic, with the guard the live code
    /// does not have. Mechanic zero means "none" and contributes nothing.
    constexpr std::uint32_t MechanicBit(std::uint32_t mechanic)
    {
        return mechanic == 0 ? 0u : (1u << (mechanic - 1));
    }
}

#endif
