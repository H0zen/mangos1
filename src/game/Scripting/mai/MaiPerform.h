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

#ifndef MANGOS_MAI_PERFORM_H
#define MANGOS_MAI_PERFORM_H

#include "MaiActor.h"
#include "MaiScript.h"

/**
 * Doing what a step says.
 *
 * The 26 verbs EventAI had and the DB scripts did not are implemented here.
 * The 47 the DB scripts had are still carried out by the body that already
 * exists, and @a handled says which of the two happened -- so the caller can
 * fall back without this file having to know how.
 */
namespace mai
{
    /// @param handled false when no native body exists and the caller should
    ///        fall through to the DB scripts' own.
    /// @return true when the sequence should stop at this step.
    bool PerformNative(Doing& doing, Step const& step, bool& handled);
}

#endif //MANGOS_MAI_PERFORM_H
