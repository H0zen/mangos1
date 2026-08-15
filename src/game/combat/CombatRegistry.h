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

#ifndef MANGOS_COMBAT_COMBATREGISTRY_H
#define MANGOS_COMBAT_COMBATREGISTRY_H

#include "ReactionQueue.h"

class Map;

namespace Combat
{
    /**
     * @brief Everything about combat that belongs to a map rather than to a
     *        unit. Today that is the reaction queue; engagements move here at
     *        stage G.
     *
     * The queue used to be a local in Unit::AttackerStateUpdate, which was
     * fine while melee was the only thing producing reactions and wrong the
     * moment spells were in scope. A triggered spell cast by a weapon proc and
     * a proc fired by that triggered spell have to share one depth counter and
     * one budget, or neither limit means anything. They share this one.
     *
     * One registry per map, so two maps updating on different threads never
     * touch the same queue. Nothing here is locked, and nothing here may be
     * reached from another map's update.
     */
    class CombatRegistry
    {
        public:
            ReactionQueue& Queue()
            {
                return m_queue;
            }

            /**
             * @brief Run whatever is still pending.
             *
             * Called twice, and both calls matter.
             *
             * A swing drains immediately after committing, which is what keeps
             * the old ordering exactly: a proc still lands between this swing
             * and the next thing the world does, not at the end of the tick.
             * ReactionQueue::Drain is re-entrancy guarded, so the outermost
             * call owns the traversal and a nested one returns.
             *
             * The map then drains again at the end of its update, for anything
             * pushed by a path that has no drain of its own -- which is every
             * spell path until stage F gives them one.
             */
            void Drain(Map& map);

            /**
             * @brief Forget everything. Called when the map is torn down.
             *
             * Ordering is the point: this runs BEFORE the grids unload, while
             * the units named in the queue still exist. A reaction drained
             * after its map has begun dissolving would resolve a guid against
             * a half-unloaded world.
             */
            void Clear()
            {
                m_queue.Clear();
            }

        private:
            ReactionQueue m_queue;
    };
}

#endif
