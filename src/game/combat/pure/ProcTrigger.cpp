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

#include "ProcTrigger.h"

#include <cstring>

namespace Combat
{
    namespace
    {
        struct TargetName
        {
            ProcTarget  target;
            char const* name;
        };

        const TargetName TARGET_NAMES[] =
        {
            { ProcTarget::Victim,           "victim" },
            { ProcTarget::Self,             "self" },
            { ProcTarget::RandomUnfriendly, "random_unfriendly" },
            { ProcTarget::NextRaidMember,   "next_raid_member" },
            { ProcTarget::AuraCaster,       "aura_caster" }
        };
    }

    char const* NameOf(ProcTarget target)
    {
        for (TargetName const& entry : TARGET_NAMES)
        {
            if (entry.target == target)
            {
                return entry.name;
            }
        }

        return "victim";
    }

    bool ParseProcTarget(char const* name, ProcTarget& out)
    {
        if (!name)
        {
            return false;
        }

        for (TargetName const& entry : TARGET_NAMES)
        {
            if (std::strcmp(entry.name, name) == 0)
            {
                out = entry.target;
                return true;
            }
        }

        return false;
    }
}
