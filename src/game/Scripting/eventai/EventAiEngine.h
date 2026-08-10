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

#ifndef MANGOS_EVENTAI_ENGINE_H
#define MANGOS_EVENTAI_ENGINE_H

#include "IScriptEngine.h"

namespace scripting
{
    /**
     * The `creature_ai_*` tables, seen through the seam.
     *
     * The exact opposite of DBScripts, and that is why it is the second engine
     * to move: DBScripts is all events and no roles, EventAI is all role and
     * no events. It never watches anything the world does -- it is handed one
     * creature, drives it for that creature's whole life, and hears about the
     * world only through the CreatureAI callbacks the world already makes
     * directly on the object it built.
     *
     * So Dispatch answers Continue to everything, and that is the whole of it:
     * an engine that subscribes to no event still has to say so, because
     * Dispatch is the one thing every engine must implement. The work is in
     * Bid and MakeCreatureAI.
     *
     * The binding is `creature_template.AIName` = "EventAI", which is per
     * TEMPLATE ENTRY rather than per spawn -- hence BidNormal and not
     * BidStrong. An engine holding a script bound to one particular creature
     * knows more about that creature than a row that names every copy of the
     * entry, and should outbid this.
     */
    class EventAiEngine : public IEngine
    {
    public:
        char const* GetName() const override { return "EventAI"; }

        /// Subscribes to nothing; every event falls through to the next engine.
        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override
        {
            (void)ctx; (void)id; (void)args; (void)count;
            return Verdict::Continue;
        }

        int Bid(Context const& ctx, RoleId role, Ref subject) override;

        CreatureAI* MakeCreatureAI(Context const& ctx,
                                   Creature* creature) override;

        void LoadData(LoadPhase phase) override;
        bool ReloadData(char const* table) override;
    };
}

#endif //MANGOS_EVENTAI_ENGINE_H
