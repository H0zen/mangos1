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

#ifndef MANGOS_COMBAT_STRIKE_H
#define MANGOS_COMBAT_STRIKE_H

#include "CombatTypes.h"

namespace Combat
{
    /**
     * @brief A fully resolved swing. Nothing here has touched the world.
     *
     * The two combatants are not named. A Strike is arithmetic, and identity
     * is the commit layer's problem -- it carries the guids beside this and
     * re-resolves them, which is what keeps a proc that despawns its target
     * from turning a pointer in here into a use-after-free.
     *
     * Every stage of mitigation is kept rather than accumulated into one
     * number. Rage, weapon skill-up and the combat log each need a different
     * point in the chain; the old path reconstructed them with subtractions
     * spread over three files and got the attacker's rage wrong.
     */
    struct Strike
    {
        Outcome       outcome    = Outcome::Miss;
        Hand          hand       = Hand::Main;
        std::uint32_t schoolMask = SCHOOL_MASK_PHYSICAL;

        std::uint32_t raw        = 0;  ///< rolled off the weapon, nothing else
        std::uint32_t afterRoll  = 0;  ///< after crit, glancing or crushing
        std::uint32_t afterArmor = 0;  ///< after armour, physical only
        std::uint32_t blocked    = 0;
        std::uint32_t absorbed   = 0;
        std::uint32_t resisted   = 0;

        /// What leaves the health bar. Valid once ApplyAbsorbResist has run.
        std::uint32_t applied = 0;

        /// The basis for rage and skill-up: damage the victim did not take
        /// because it avoided, blocked, armoured or resisted the swing.
        std::uint32_t clean = 0;

        /// True once absorb and resist have been folded in. The commit layer
        /// asserts on it rather than trusting call order.
        bool finalised = false;

        /**
         * @brief Fold in the absorb and resist the aura system reported.
         *
         * The pure core cannot compute these -- they are a walk of the
         * victim's absorb auras -- so the world hands them back and the
         * arithmetic stays here rather than being repeated at the call site.
         */
        void ApplyAbsorbResist(std::uint32_t absorb, std::uint32_t resist);

        /// Damage was rolled and the victim took none of it.
        bool Avoided() const
        {
            return IsAvoidance(outcome);
        }
    };
}

#endif
