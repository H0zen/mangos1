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

#include "GameObjectUseRegistry.h"

#include "GameObjectUseHandlers.h"
#include "SharedDefines.h"
#include "Utilities/Errors.h"

#include <array>
#include <cstddef>

namespace GameObjectUse
{
    namespace
    {
        struct Registration
        {
            std::uint32_t goType;
            Handler       handler;
        };

        /// Which type does what. A type absent from this list has no handler
        /// yet and is still answered by the switch in GameObject::Use.
        const Registration REGISTRATIONS[] =
        {
            { GAMEOBJECT_TYPE_DOOR,         &Types::Door },
            { GAMEOBJECT_TYPE_BUTTON,       &Types::Button },
            { GAMEOBJECT_TYPE_QUESTGIVER,   &Types::QuestGiver },
            { GAMEOBJECT_TYPE_CHEST,        &Types::Chest },
            { GAMEOBJECT_TYPE_GENERIC,      &Types::Generic },
            { GAMEOBJECT_TYPE_SPELL_FOCUS,  &Types::SpellFocus },
            { GAMEOBJECT_TYPE_CAMERA,       &Types::Camera },
            { GAMEOBJECT_TYPE_SPELLCASTER,  &Types::SpellCaster },
            { GAMEOBJECT_TYPE_MEETINGSTONE, &Types::MeetingStone },
            { GAMEOBJECT_TYPE_FLAGSTAND,    &Types::FlagStand },
            { GAMEOBJECT_TYPE_FISHINGHOLE,  &Types::FishingHole }
        };

        constexpr std::size_t REGISTRATION_COUNT =
            sizeof(REGISTRATIONS) / sizeof(REGISTRATIONS[0]);

        /// One past the largest GAMEOBJECT_TYPE_*. The table is indexed by
        /// type, so it has to cover them all rather than only the handled
        /// ones.
        constexpr std::size_t TYPE_COUNT = MAX_GAMEOBJECT_TYPE;

        class Table
        {
            public:
                Table()
                {
                    m_slots.fill(nullptr);

                    for (std::size_t i = 0; i < REGISTRATION_COUNT; ++i)
                    {
                        Registration const& entry = REGISTRATIONS[i];

                        MANGOS_ASSERT(entry.goType < TYPE_COUNT);
                        MANGOS_ASSERT(m_slots[entry.goType] == nullptr);

                        m_slots[entry.goType] = entry.handler;
                    }
                }

                Handler At(std::uint32_t goType) const
                {
                    return goType < TYPE_COUNT ? m_slots[goType] : nullptr;
                }

            private:
                std::array<Handler, TYPE_COUNT> m_slots{};
        };

        Table const& Lookup()
        {
            static Table const table;
            return table;
        }
    }

    Handler HandlerFor(std::uint32_t goType)
    {
        return Lookup().At(goType);
    }

    std::uint32_t RegisteredHandlerCount()
    {
        return static_cast<std::uint32_t>(REGISTRATION_COUNT);
    }
}
