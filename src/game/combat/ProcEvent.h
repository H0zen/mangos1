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

#ifndef MANGOS_COMBAT_PROCEVENT_H
#define MANGOS_COMBAT_PROCEVENT_H

#include "ObjectGuid.h"

#include <cstdint>

class Aura;
class Item;
class Player;
class Unit;
struct SpellEntry;

namespace Combat
{
    /// What a proc handler decided.
    enum class ProcResult : std::uint8_t
    {
        /// Handled. A charge may be spent.
        Ok = 0,

        /// Not handled. Charges are kept.
        Failed = 1,

        /// This aura cannot proc from this event at all. Skipped without
        /// touching charges, and without counting as a failure for the
        /// other effects of the same holder.
        CantTrigger = 2
    };

    /**
     * @brief One proc, as a value.
     *
     * @ref actor is the unit whose aura is procing; @ref target is the other
     * end of whatever caused it. Both may be the same unit.
     *
     * The helpers below are not decoration. Nearly every handler opens by
     * resolving the cast item and closes by casting a triggered spell under a
     * cooldown check -- the same twenty lines, fifteen times over. They live
     * here so a handler is only the part that is actually specific to it.
     */
    struct ProcEvent
    {
        Unit* actor  = nullptr;
        Unit* target = nullptr;
        Aura* aura   = nullptr;

        /// The spell that caused the proc. Null for a melee swing.
        SpellEntry const* procSpell = nullptr;

        std::uint32_t damage   = 0;
        std::uint32_t flags    = 0;
        std::uint32_t extra    = 0;

        /// Seconds to lock the triggered spell for, zero for none. Only
        /// players carry proc cooldowns.
        std::uint32_t cooldown = 0;

        // -- what the aura is ----------------------------------------------

        /// The spell the procing aura belongs to.
        SpellEntry const* AuraSpell() const;

        /// The aura's current modifier amount.
        std::int32_t Amount() const;

        /// The aura's unmodified base points.
        std::int32_t BasePoints() const;

        // -- what the actor is ---------------------------------------------

        /// The actor as a player, or null when it is not one.
        Player* ActorPlayer() const;

        /// The item the aura was cast from, or null. Already gated on the
        /// actor being a player, which is the only way an item aura exists.
        Item* CastItem() const;

        // -- cooldown ------------------------------------------------------

        /// True when @ref cooldown applies and the actor is already locked
        /// out of @p spellId.
        bool OnCooldown(std::uint32_t spellId) const;

        /// Lock the actor out of @p spellId for @ref cooldown seconds. Does
        /// nothing when there is no cooldown or the actor is not a player.
        void StartCooldown(std::uint32_t spellId) const;

        // -- the standard ending -------------------------------------------

        /**
         * @brief Cast a triggered spell as the close of a proc.
         *
         * Refuses, without casting, when the spell does not exist, when the
         * victim is a dead unit other than the actor itself, or when the
         * cooldown is still running. Starts the cooldown on success.
         *
         * @param victim     Who to cast at.
         * @param spellId    The triggered spell.
         * @param basePoints Overrides the spell's own base points on effect
         *                   zero when given.
         * @param caller     Named in the log if @p spellId does not exist.
         *
         * @return Ok when it cast, Failed otherwise.
         */
        ProcResult Trigger(Unit* victim, std::uint32_t spellId,
                           std::int32_t const* basePoints = nullptr,
                           char const* caller = nullptr) const;
    };
}

#endif
