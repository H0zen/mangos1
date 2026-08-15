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

#ifndef MANGOS_MAI_COMPILE_H
#define MANGOS_MAI_COMPILE_H

#include "MaiScript.h"

#include <string>
#include <vector>

/**
 * Turning blocks into jumps, once, when the script loads.
 *
 * `if`/`else`/`end`, `repeat`/`end` and `while`/`end` are what somebody types
 * into the `action` column. They nest, they have to balance, and the runner
 * must never have to think about any of that: a branch at run time is an index
 * and a comparison, and everything that could have been got wrong about it was
 * got wrong here instead -- at load, with the row named.
 *
 * THAT IS THE WHOLE ARGUMENT FOR THIS BEING A COMPILER AND NOT AN INTERPRETER.
 * MaiValidate can hold a script up against the world and say "spell 12345 does
 * not exist"; this holds it up against itself and says "the `if` at seq 3 is
 * never closed". Both are things a program in C++ cannot be asked at all, and
 * both happen before a player is standing in front of the result.
 *
 * What comes out is the same vector of steps with Step::flow, Step::jump and
 * Step::loopSlot filled in, and Sequence::program set. A sequence with no
 * control verb in it comes out untouched -- and is left sorted by time, which
 * is what the 27,561 converted steps are and must remain.
 */
namespace mai
{
    /// Whether @a id is one of the seven that branch rather than act.
    bool IsControl(ActionId id);

    /// Whether anything in @a steps branches, and therefore whether the list
    /// is a program to be read in `seq` order rather than a timeline to sort.
    bool Branches(std::vector<Step> const& steps);

    /**
     * Resolve @a sequence's blocks into jumps, in place.
     *
     * @return false, with @a error naming the row and what is wrong with it,
     *         leaving the sequence unfit to run. Every caller refuses the
     *         script rather than running half of it: a program with one arm of
     *         a branch missing is not a smaller program, it is a different one.
     */
    bool Compile(Sequence& sequence, std::string& error);
}

#endif //MANGOS_MAI_COMPILE_H
