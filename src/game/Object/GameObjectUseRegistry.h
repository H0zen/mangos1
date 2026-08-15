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

#ifndef MANGOS_H_GAMEOBJECTUSEREGISTRY
#define MANGOS_H_GAMEOBJECTUSEREGISTRY

#include <cstdint>

class GameObject;
class Unit;

/**
 * @brief What happens when somebody uses a game object, one handler per type.
 *
 * A door opens, a chair is sat in, a bobber is reeled in, a summoning circle
 * counts how many people are standing in it. Seventeen genuinely different
 * behaviours that share only their beginning and their end -- so what is
 * shared stays in GameObject::Use and what differs is a free function per
 * type, reached through a table.
 *
 * NOT a data table in disguise. The proc effects that moved into
 * `spell_proc` rows were the same behaviour repeated with different spell
 * ids; these are seventeen different behaviours, and a row could not say what
 * a fishing bobber does.
 */
namespace GameObjectUse
{
    /// One use, as a value.
    struct Use
    {
        GameObject* object = nullptr;
        Unit*       user   = nullptr;

        /// A script already produced the behaviour for this use. The types
        /// that run an activation script of their own skip it when set.
        bool        claimed = false;
    };

    /// What a handler decided the use amounts to. Everything a type does
    /// beyond this it does itself; this is only the part GameObject::Use
    /// finishes on its behalf.
    struct Outcome
    {
        /// Who casts @ref spellId. The user unless a handler says otherwise.
        Unit* caster = nullptr;

        /// Zero means nothing is cast, which is true of most types.
        std::uint32_t spellId = 0;

        bool triggered = false;
    };

    using Handler = void (*)(Use const& use, Outcome& outcome);

    /**
     * @brief The handler for a GAMEOBJECT_TYPE_*.
     *
     * Null when the type has none, which the caller reports -- an object
     * whose type nothing handles is a data error worth a line in the log,
     * not a silent no-op.
     */
    Handler HandlerFor(std::uint32_t goType);

    /// How many types carry a handler. For the boot log and the tests.
    std::uint32_t RegisteredHandlerCount();
}

#endif
