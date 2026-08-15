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

#ifndef MANGOS_COMBAT_PROFILEBUILDER_H
#define MANGOS_COMBAT_PROFILEBUILDER_H

#include "combat/pure/CombatTypes.h"
#include "combat/pure/Profile.h"

class Unit;

/**
 * @brief The one place the world is read into the combat core.
 *
 * Everything above this file is pure: it takes numbers and returns numbers.
 * Everything below it is the server. This is the seam, and it is deliberately
 * one function wide -- if a rule needs something new from a Unit, it is added
 * to Profile and read here, never reached for from inside the core.
 */
namespace Combat
{
    /**
     * @brief Read a unit's current combat numbers into a Profile.
     *
     * Expensive by design: this is the aura walking, the item reading and the
     * skill arithmetic that the old path did on every swing. It is meant to
     * happen when something changes, not when something is hit.
     *
     * Describes the unit and nobody else. Whoever it ends up fighting changes
     * nothing here, which is what lets one profile be kept and reused instead
     * of rebuilt per swing.
     */
    Profile BuildProfile(Unit const* unit);

    /**
     * @brief What @a attacker adds to a crit against @a victim's TYPE.
     *
     * The one modifier that genuinely belongs to the pair: it is an aura on
     * the attacker selected by the victim's creature type, so it can be
     * neither cached on one nor derived from the other. Added to the matchup
     * at the swing.
     */
    Hundredths CritDamageVersus(Unit const* attacker, Unit const* victim);

    /**
     * @brief The facts about a swing that belong to neither combatant.
     *
     * Geometry is resolved here, in the shared frame, and handed to the core
     * as three booleans. The core never sees a position, so it can never
     * compose one across a vessel boundary.
     */
    Situation BuildSituation(Unit const* attacker, Unit const* victim);
}

#endif
