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

#ifndef MANGOS_MAI_PARSE_H
#define MANGOS_MAI_PARSE_H

#include "MaiRule.h"
#include "MaiScript.h"

#include <string>
#include <vector>

/**
 * Reading a step back out of the table.
 *
 * `params` is "name=value name=value", with every name and type taken from
 * actions.manifest. That is what lets the same column be readable to a person
 * and checkable by a machine: a misspelt parameter is a load error naming the
 * parameter and the verb, and so is a value of the wrong shape. Neither is
 * expressible in six columns called datalong.
 */
namespace mai
{
    /// The verb @a name names, or ActionId::None.
    ActionId ActionNamed(char const* name);

    /// The trigger @a name names, or RuleId::None.
    RuleId RuleNamed(char const* name);

    /// @return false with @a error naming what is wrong and where. Strict: an
    ///         unknown parameter is refused rather than ignored.
    bool Parse(char const* action, char const* params, Step& out,
               std::string& error);

    /// The same, for a rule's trigger and its parameters. One implementation
    /// serves both: ActionSpec and RuleSpec are the same shape because one
    /// generator emits them, so reading `percent_max=30` is the same operation
    /// as reading `spell=11962`.
    bool Parse(char const* trigger, char const* params, Rule& out,
               std::string& error);

    /// The same again, for a step whose verb names one of the creature's own
    /// remembered numbers. @a owner is where such a name is interned, and is
    /// required for exactly the two verbs that take one.
    bool Parse(char const* action, char const* params, Step& out,
               RuleSet& owner, std::string& error);

    /**
     * A guard: `enraged=0`, or `kills>=3 phase!=2`.
     *
     * All of them must hold. The operators are the six a comparison has and
     * nothing else -- see Guard in MaiScript.h for why there is deliberately
     * no `or` and no nesting.
     *
     * ONE PARSER FOR TWO PLACES. This was a rule's column and is now also a
     * step's, which is what makes `if` a verb rather than a feature: the row
     * that decides whether a whole rule fires and the row that decides whether
     * one step runs say the same thing the same way. @a owner is where a bare
     * name is interned and may be null -- a sequence the world starts has no
     * creature, so a guard about a creature's memory is refused there rather
     * than being quietly read as zero.
     */
    bool ParseGuards(char const* text, std::vector<Guard>& out, RuleSet* owner,
                     std::string& error);

    /// The same, appending to a rule's own guards.
    bool ParseGuards(char const* text, Rule& out, RuleSet& owner,
                     std::string& error);
}

#endif //MANGOS_MAI_PARSE_H
