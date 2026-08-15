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

#ifndef MANGOS_SCRIPT_EVENT_IDS_H
#define MANGOS_SCRIPT_EVENT_IDS_H

/**
 * Every event id the world is capable of raising.
 *
 * Gathered from the things that can raise one: a goober, a chest, a camera, a
 * capture point or a destructible building naming an eventId in its template,
 * a spell with SPELL_EFFECT_SEND_EVENT, and a taxi path node.
 *
 * It belongs to neither engine, which is why it is here and not in one of the
 * nests. Both check their own tables against it -- `dbscripts_on_event` so a
 * row cannot name an id nothing will ever raise, and `script_binding` so a
 * MAPEVENT binding cannot either -- and it was private to ScriptMgr precisely
 * because ScriptMgr was both of them at once. Duplicating it into each nest
 * would be two answers to one question about the world.
 */

#include "Platform/Define.h"

#include <set>

void CollectPossibleEventIds(std::set<uint32>& eventIds);

#endif //MANGOS_SCRIPT_EVENT_IDS_H
