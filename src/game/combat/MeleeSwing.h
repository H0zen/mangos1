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

#ifndef MANGOS_COMBAT_MELEESWING_H
#define MANGOS_COMBAT_MELEESWING_H

#include "ReactionQueue.h"
#include "combat/pure/CombatTypes.h"

class Map;
class Unit;

namespace Combat
{
    /**
     * @brief One white swing, end to end.
     *
     * Reads both profiles, builds the matchup and the table, resolves the
     * strike, asks the aura system for absorb and resist, and commits. Every
     * consequence goes into @p queue; nothing runs from inside here.
     *
     * @param depth How deep in a reaction chain this swing already is. A swing
     *              the player's attack timer started is zero; one an extra
     *              attack granted is one. Stamped onto everything the commit
     *              enqueues, so the chain terminates by arithmetic.
     */
    void PerformSwing(Unit& attacker, Unit& victim, Hand hand,
                      ReactionQueue& queue, std::uint8_t depth = 0);

    /**
     * @brief The pre-rewrite white swing, applied rather than described.
     *
     * This is what CombatShadow = 1 runs, and until now that setting was a
     * lie: the config called mode 1 "compare, apply the OLD answer" and the
     * code applied the new one either way, so an operator who reached for it
     * as a rollback got the rewrite with a log beside it.
     *
     * The old engine is still in the tree -- Unit::CalculateMeleeDamage and
     * Unit::DealMeleeDamage were never touched -- so mode 1 now calls it, in
     * the order it used: damage mods, log, procs, damage. Nothing goes on the
     * reaction queue, because the old path had no queue and running half of
     * each engine would be a third behaviour nobody has tested.
     *
     * The band comparison still happens first, and it is the one thing both
     * modes share: what is reported is the same either way, and only the
     * answer that gets applied changes.
     *
     * Removed with the rest of the scaffolding at stage M.
     */
    void PerformLegacySwing(Unit& attacker, Unit& victim, Hand hand);

    /**
     * @brief Runs queued reactions against the world.
     *
     * Both ends of every reaction are re-resolved from their guid here. A
     * reaction whose source or target has gone is dropped rather than
     * followed, and that is the whole of the lifetime handling -- there is no
     * pointer in the queue to dangle.
     */
    class WorldReactionSink : public ReactionSink
    {
        public:
            explicit WorldReactionSink(Map& map) : m_map(map) {}

            void Run(Reaction const& reaction, ReactionQueue& queue) override;

        private:
            Map& m_map;
    };
}

#endif
