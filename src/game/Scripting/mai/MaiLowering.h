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

#ifndef MANGOS_MAI_LOWERING_H
#define MANGOS_MAI_LOWERING_H

#include "MaiScript.h"

#include "dbscripts/DbScripts.h"

#include <string>

/**
 * The bridge from the tables that exist to the model that replaces them.
 *
 * Temporary by intent. It exists so that MAI can be proved against the DB
 * scripts on the DB scripts' own data -- every row a live server has, lowered
 * and run, and the two engines' traces compared -- rather than against a
 * handful of examples someone wrote to make it pass. When the tables are gone
 * this file goes with them.
 *
 * @return false with @a error set when a row cannot be represented, which is a
 *         finding rather than a failure: it names a command the manifest does
 *         not have, or a shape the two disagree about, and either is something
 *         to fix before anything runs.
 */
namespace mai
{
    bool Lower(ScriptInfo const& row, Step& out, std::string& error);

    bool Lower(ScriptChain const& chain, uint32 id, char const* name,
               Sequence& out, std::string& error);

    /**
     * A step, written back out as the row it would have been.
     *
     * The inverse of the first Lower, and it exists for a reason that is
     * temporary in the same way the rest of this file is. A step that came
     * from `dbscripts_on_*` carries a pointer to the row it was lowered from,
     * and the borrowed effect bodies read that row. A step that came from a
     * RULE has no row -- EventAI's tables are a different shape entirely -- so
     * the twelve verbs whose bodies are still ScriptAction's would have had
     * nothing to read, and a creature converted to MAI could set its phase and
     * change its threat but could not talk, cast or summon.
     *
     * So the row is manufactured. It is honest scaffolding rather than a
     * trick: the manifest was extracted from the very union this fills in, so
     * the mapping is the same table read backwards, and it disappears verb by
     * verb as each grows a native body -- the last one to go takes this with
     * it.
     *
     * @return false when the step's own shape and the manifest disagree, which
     *         can only happen if one of them was edited without the other.
     */
    bool Raise(Step const& step, ScriptInfo& out, std::string& error);
}

#endif //MANGOS_MAI_LOWERING_H
