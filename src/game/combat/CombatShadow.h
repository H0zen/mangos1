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

#ifndef MANGOS_COMBAT_COMBATSHADOW_H
#define MANGOS_COMBAT_COMBATSHADOW_H

#include "combat/pure/CombatTypes.h"
#include "combat/pure/Matchup.h"

#include <cstdint>

class Unit;

/**
 * @brief Scaffolding for the combat transition. Temporary by design.
 *
 * The engine being replaced had no tests and no reference implementation but
 * itself, so a rewritten rule cannot be switched on confidence. This runs the
 * old answer beside the new one on live traffic and reports where they part.
 *
 * Two rules keep it honest.
 *
 * First, ONLY SIDE-EFFECT-FREE THINGS ARE RUN TWICE. The old path cannot
 * simply be called again: CalculateMeleeDamage rolls the dice and, worse,
 * CalculateDamageAbsorbAndResist consumes absorb shields. Running it in
 * shadow would eat a Power Word: Shield twice per swing. So what is compared
 * is not the outcome -- which is random and would prove nothing from a single
 * sample anyway -- but the numbers that go INTO the roll. If the bands agree,
 * the outcomes agree in distribution, and the bands are pure arithmetic over
 * the same accessors.
 *
 * Second, the intended divergences are declared. Five differences are
 * deliberate and are never reported; anything else is a defect. They are
 * listed against CombatShadow in mangosd.conf.dist.
 *
 * Removed by stage M. See src/game/combat/COMBAT.md.
 */
namespace Combat
{
    enum class ShadowMode : std::uint8_t
    {
        Off      = 0,
        Shadow   = 1,   ///< compare, apply the old answer
        Switched = 2    ///< compare, apply the new answer
    };

    ShadowMode CurrentShadowMode();

    /// Cheap enough to call on the swing path when the mode is Off.
    inline bool ShadowWanted()
    {
        return CurrentShadowMode() != ShadowMode::Off;
    }

    /**
     * @brief Compare the new hit table against the bands the old roll used.
     *
     * Recomputes what RollMeleeOutcomeAgainst would have worked from, using
     * the same public accessors it uses, and reports each band that differs
     * by more than a rounding step.
     *
     * No dice are rolled and nothing is consumed.
     */
    void ShadowMeleeChances(Unit const& attacker, Unit const& victim,
                            Hand hand, Matchup const& fresh);

    /// Divergences seen since start-up, for the console command and the log.
    std::uint64_t ShadowDivergenceCount();
}

#endif
