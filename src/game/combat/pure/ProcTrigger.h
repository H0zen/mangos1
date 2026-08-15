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

#ifndef MANGOS_COMBAT_PROCTRIGGER_H
#define MANGOS_COMBAT_PROCTRIGGER_H

#include "ProcPoints.h"

#include <cstdint>

namespace Combat
{
    /// Who a triggered proc spell is cast at.
    enum class ProcTarget : std::uint8_t
    {
        /// The other end of the event that procced.
        Victim = 0,

        /// The unit that owns the aura.
        Self,

        /// A random hostile within melee reach.
        RandomUnfriendly,

        /// A random raid member in range.
        NextRaidMember,

        /// Whoever applied the aura.
        AuraCaster,

        Count
    };

    /**
     * @brief One row of the proc-effect table.
     *
     * The conditions under which a proc fires already live in
     * `spell_proc_event`. This is the other half: what the proc DOES.
     *
     * Two paths. With @ref behaviour zero the generic one runs -- compute the
     * points from @ref points, pick the target from @ref target, cast
     * @ref triggerSpell. With a behaviour set, a named function runs instead,
     * for the handful of procs that carry real arithmetic rather than a
     * coefficient.
     */
    struct ProcTrigger
    {
        /// The aura spell that procs.
        std::uint32_t spellId = 0;

        /// Which effect indices of that spell this row applies to.
        std::uint32_t effectMask = 0x7;

        /// What gets cast. Zero is legal only with a behaviour set.
        std::uint32_t triggerSpell = 0;

        ProcTarget target = ProcTarget::Victim;

        PointsFormula points;

        /// Registry id of a named behaviour, or zero for the generic path.
        /// Resolved from the row's name when the table is loaded.
        std::uint16_t behaviour = 0;

        /// True when a row exists. A miss returns an all-zero trigger with
        /// this false rather than a null pointer.
        bool known = false;

        /// True when this row runs a named algorithm.
        bool IsNamed() const
        {
            return behaviour != 0;
        }
    };

    char const* NameOf(ProcTarget target);
    bool ParseProcTarget(char const* name, ProcTarget& out);
}

#endif
