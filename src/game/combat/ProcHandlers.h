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

#ifndef MANGOS_COMBAT_PROCHANDLERS_H
#define MANGOS_COMBAT_PROCHANDLERS_H

#include "ProcEvent.h"

/**
 * @brief The proc handlers, one free function per aura type that has one.
 *
 * Registered in ProcRegistry.cpp, which is the only place that knows which
 * aura type maps to which of these.
 */
namespace Combat
{
    namespace Procs
    {
        // -- small handlers, whole ----------------------------------------

        /// Blade Flurry and its relatives: a melee-haste aura that triggers.
        ProcResult Haste(ProcEvent const& e);

        /// Deals the aura's amount as spell damage to the target.
        ProcResult TriggerDamage(ProcEvent const& e);

        /// Trinket and set bonuses keyed by class-script id.
        ProcResult OverrideClassScript(ProcEvent const& e);

        /// Prayer of Mending: heal, then jump to another raid member.
        ProcResult Mending(ProcEvent const& e);

        /// Passes only for a spell with a cast time.
        ProcResult CastingSpeedNotStack(ProcEvent const& e);

        /// Passes only for a spell of the reflected school.
        ProcResult ReflectSpellsSchool(ProcEvent const& e);

        /// Passes only for a spell of the right school that costs power.
        ProcResult PowerCostSchool(ProcEvent const& e);

        /// Passes only for a spell carrying the aura's mechanic.
        ProcResult MechanicImmunity(ProcEvent const& e);

        /// Incanter's Regalia on top of Mana Shield.
        ProcResult ManaShield(ProcEvent const& e);

        /// Hunter's Mark growing as it is hit.
        ProcResult AttackPowerAttackerBonus(ProcEvent const& e);

        /// Inner Fire and friends: only real damage counts.
        ProcResult ModResistance(ProcEvent const& e);

        /// Fear, Root and Pacify+Silence breaking on damage taken.
        ProcResult RemoveByDamageChance(ProcEvent const& e);

        /// Invisibility dropping when its own spell procs.
        ProcResult Invisibility(ProcEvent const& e);

        /// Aura types whose effect is a spell_proc_trigger row: the row picks
        /// the target, computes the base points and names the spell to cast.
        ProcResult Dummy(ProcEvent const& e);
        ProcResult TriggerSpell(ProcEvent const& e);
    }
}

#endif
