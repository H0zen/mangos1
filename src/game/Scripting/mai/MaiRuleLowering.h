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

#ifndef MANGOS_MAI_RULE_LOWERING_H
#define MANGOS_MAI_RULE_LOWERING_H

#include "MaiRule.h"

#include <string>

struct CreatureEventAI_Action;
struct CreatureEventAI_Event;

/**
 * EventAI's rows into MAI rules.
 *
 * Temporary, exactly as MaiLowering is: it exists so the rule engine can be
 * proved against twenty thousand rows a live world already has, rather than
 * against examples written to make it pass. When mai_rule is what gets read,
 * this goes.
 */
namespace mai
{
    /// One of a row's three action slots, as the step it means.
    ///
    /// Exposed rather than kept private because it is the half that needs
    /// checking: the trigger mapping is a switch over numbers, and this is a
    /// fifty-row table asserting which of EventAI's columns means what.
    bool Lower(CreatureEventAI_Action const& action, Step& out,
               std::string& error);

    bool Lower(CreatureEventAI_Event const& row, Rule& out, std::string& error);
}

#endif //MANGOS_MAI_RULE_LOWERING_H
