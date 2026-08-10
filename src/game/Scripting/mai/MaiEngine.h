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

#ifndef MANGOS_MAI_ENGINE_H
#define MANGOS_MAI_ENGINE_H

#include "IScriptEngine.h"

#include "mai/MaiScript.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

class WorldObject;

namespace scripting
{
    /**
     * MAI, running the DB scripts' own tables through MAI's model.
     *
     * It is the DB scripts' engine now, not a second opinion beside one:
     * DbScriptEngine is gone, the tables are read through MAI's model, and the
     * differential test over every chain a live world has is what made the
     * swap something other than an act of faith.
     *
     * IT KEEPS ITS OWN SCHEDULE, and does not touch Map::m_scriptSchedule.
     * Sharing one would have meant editing Map to know about a second engine,
     * which is exactly what the seam exists to prevent; and a frame here is a
     * different shape anyway -- one record per RUN, walked by the runner, with
     * no priority queue at all. Tick() already arrives per map with the map's
     * own diff, so there is nothing a shared queue would provide.
     *
     * A map's frames die with the map, in RetireState. Nothing survives it: a
     * sequence that was still running when the last player left is a sequence
     * about a world that no longer exists.
     */
    class MaiEngine : public IEngine
    {
    public:
        char const* GetName() const override { return "MAI"; }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override;

        void LoadData(LoadPhase phase) override;
        bool ReloadData(char const* table) override;
        void Tick(Context const& ctx, uint32 diff) override;
        void RetireState(Context const& ctx) override;

    private:
        /// The sequences, lowered from `db_scripts` and validated once.
        /// Keyed by the same (type, id) the DB scripts use, because that is
        /// what the events resolve to.
        struct Key
        {
            uint32 type;
            uint32 id;

            bool operator==(Key const& other) const
            {
                return type == other.type && id == other.id;
            }
        };

        struct KeyHash
        {
            std::size_t operator()(Key const& key) const
            {
                return std::size_t(key.type) * 1000003u + key.id;
            }
        };

        /// @return false when nothing was started -- there is no such
        ///         sequence, or one is already running for this actor and the
        ///         engine's own policy says one is enough.
        bool Start(Map* map, uint32 type, uint32 id, WorldObject* source,
                   WorldObject* target, uint32 unique);

        std::unordered_map<Key, mai::Sequence, KeyHash> m_sequences;
        std::unordered_map<Map const*, std::vector<mai::Frame>> m_frames;
    };
}

#endif //MANGOS_MAI_ENGINE_H
