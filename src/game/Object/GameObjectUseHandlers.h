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

#ifndef MANGOS_H_GAMEOBJECTUSEHANDLERS
#define MANGOS_H_GAMEOBJECTUSEHANDLERS

#include "GameObjectUseRegistry.h"

/**
 * @brief The per-type behaviours, one free function each.
 *
 * Registered in GameObjectUseRegistry.cpp, which is the only place that knows
 * which GAMEOBJECT_TYPE_* maps to which of these.
 */
namespace GameObjectUse
{
    namespace Types
    {
        /// Opens or closes, and runs its activation script. Never despawns.
        void Door(Use const& use, Outcome& outcome);

        /// A door that also trips whatever it is linked to.
        void Button(Use const& use, Outcome& outcome);

        /// Offers its gossip menu.
        void QuestGiver(Use const& use, Outcome& outcome);

        /// Trips its linked object and starts its event; the loot itself is
        /// opened elsewhere.
        void Chest(Use const& use, Outcome& outcome);

        /// Deactivates, unless a script already answered for it.
        void Generic(Use const& use, Outcome& outcome);

        /// Trips its linked object and nothing else.
        void SpellFocus(Use const& use, Outcome& outcome);

        /// Plays a cinematic and starts an event.
        void Camera(Use const& use, Outcome& outcome);

        /// Casts its own spell, locking itself first, optionally only for the
        /// owner's party.
        void SpellCaster(Use const& use, Outcome& outcome);

        /// Summons a party member within the stone's level range.
        void MeetingStone(Use const& use, Outcome& outcome);

        /// A battleground flag in its stand.
        void FlagStand(Use const& use, Outcome& outcome);

        /// Opens as loot rather than as an object.
        void FishingHole(Use const& use, Outcome& outcome);
    }
}

#endif
