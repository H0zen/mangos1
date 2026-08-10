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

#ifndef MANGOS_SD3_ENGINE_H
#define MANGOS_SD3_ENGINE_H

#include "IScriptEngine.h"

namespace scripting
{
    /**
     * ScriptDev3 -- the C++ scripts -- seen through the seam.
     *
     * The last producer to move behind IEngine, and the one the seam was
     * shaped around: every comment in ScriptTypes.h that says "an engine that
     * can answer precisely should" or "a bidder may decline after all" was
     * written with this one in mind.
     *
     * It is the only engine so far that does both halves of the job. It bids
     * for all three roles, and it subscribes to nearly every claimable event
     * in the manifest. Both come from the same binding: an object's
     * `ScriptName`, resolved to a script id by the core and looked up in SD3's
     * own table.
     *
     * BidStrong, and above EventAI deliberately. A script name is attached to
     * one creature template on purpose by whoever wrote the script; an AIName
     * of "EventAI" says only that the entry has rows in a table. That ordering
     * is not new -- SD3 was consulted at the top of FactorySelector::selectAI
     * and EventAI only reached through the AI registry below it -- it is just
     * a number now instead of the shape of the function.
     *
     * Declining after bidding is normal here and not a failure: SD3 bids on
     * having a script bound to the entry, and that script's own GetAI() may
     * still return nothing. The auction then falls through to the next bidder,
     * which is why the two phases are separate.
     */
    class Sd3Engine : public IEngine
    {
    public:
        char const* GetName() const override { return "ScriptDev3"; }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override;

        int Bid(Context const& ctx, RoleId role, Ref subject) override;

        CreatureAI*   MakeCreatureAI(Context const& ctx,
                                     Creature* creature) override;
        GameObjectAI* MakeGameObjectAI(Context const& ctx,
                                       GameObject* go) override;
        InstanceData* MakeInstanceData(Context const& ctx, Map* map) override;

        void LoadData(LoadPhase phase) override;
        bool ReloadData(char const* table) override;
    };
}

#endif //MANGOS_SD3_ENGINE_H
