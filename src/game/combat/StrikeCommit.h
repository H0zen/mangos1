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

#ifndef MANGOS_COMBAT_STRIKECOMMIT_H
#define MANGOS_COMBAT_STRIKECOMMIT_H

#include "ObjectGuid.h"
#include "ReactionQueue.h"
#include "combat/pure/Strike.h"

class Unit;

namespace Combat
{
    /// A resolved strike, with the two units it is about.
    ///
    /// Strike itself names nobody: it is arithmetic, and it lives in a library
    /// that has never heard of an ObjectGuid. Identity is attached here, at
    /// the layer that has to survive the target being deleted mid-swing.
    struct StrikeOrder
    {
        ObjectGuid victim;
        Strike     strike;
    };

    /// What the commit did. Enough for the caller to decide whether to keep
    /// swinging, and nothing that outlives the call.
    struct CommitResult
    {
        bool resolved   = false;  ///< both ends were still there
        bool applied    = false;  ///< health actually moved
        bool victimDied = false;
    };

    /**
     * @brief The one place a strike changes the world.
     *
     * The phases are the contract, and they run in this order:
     *
     *   1. Resolve   -- the victim guid, again, from scratch
     *   2. Log       -- SMSG_ATTACKERSTATEUPDATE, with the resolved numbers
     *   3. Health    -- one DealDamage
     *   4. Threat    -- still inside DealDamage; see below
     *   5. Death     -- if the victim fell, drop every reaction naming it
     *   6. React     -- enqueue procs, shields, weapon spells, daze, extras
     *   7. Notify    -- AttackedBy, once
     *
     * Phase 4 is not separated yet, and pretending otherwise would be a lie:
     * DealDamage in this core also does threat, rage, the AI notification and
     * the kill. Taking it apart is a later stage. What this class fixes now is
     * the OUTER order -- the old path ran the whole proc machinery between
     * phase 2 and phase 3, which is where "the log says 400 but the target was
     * already dead" and the use-after-free on despawn both came from.
     *
     * Nothing here recurses. A consequence goes in the queue.
     */
    class StrikeCommit
    {
        public:
            static CommitResult Apply(Unit& attacker, StrikeOrder const& order,
                                      ReactionQueue& queue);
    };
}

#endif
