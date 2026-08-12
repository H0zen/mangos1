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

#include "ObjectGuid.h"

#include "mai/MaiRule.h"
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
        MaiEngine();
        ~MaiEngine() override;

        char const* GetName() const override { return "MAI"; }

        /// What `mai::StartSequence` reaches, so that a VERB can start another
        /// sequence without including the engine. There is exactly one engine
        /// per process -- ScriptHost makes it and owns it -- so the indirection
        /// is a pointer rather than a lookup.
        static bool StartFrom(Map* map, uint32 kind, uint32 id,
                              WorldObject* source, WorldObject* target,
                              ObjectGuid owner, ObjectGuid item, bool* cancel);

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override;

        int Bid(Context const& ctx, RoleId role, Ref subject) override;

        CreatureAI* MakeCreatureAI(Context const& ctx,
                                   Creature* creature) override;

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
                   WorldObject* target, uint32 unique,
                   ObjectGuid owner = ObjectGuid());

        /**
         * Run a sequence NOW, rather than queueing it.
         *
         * Only one kind uses this and it is the reason the kind exists: an
         * item's use has to be answered before the item's spell goes ahead,
         * and an answer that arrives on the next map tick is not an answer.
         *
         * The steps at time zero run here, in order, and stop where a step
         * says to. Anything with a later time is queued from where the inline
         * part left off, so "say no, or else do this two seconds later" is
         * still one sequence -- unless the refusal happened, in which case
         * there is nothing left to do.
         *
         * @return true when a step REFUSED, which the caller turns into a
         *         cancel.
         */
        bool RunNow(Map* map, uint32 type, uint32 id, WorldObject* source,
                    WorldObject* target, ObjectGuid owner, ObjectGuid item);

        static MaiEngine* s_instance;

        /// Whether this world database carries MAI's five tables. False means
        /// the migration has not been applied, which is reported once instead
        /// of as ten lines of SQL errors and four "table is empty" notices.
        static bool HasSchema();

        /// Everything anything says, from `mai_text`.
        void LoadTexts();

        /// The sequences, lowered from `db_scripts`. Re-runnable: a reload
        /// clears the frames that point into them first.
        void LoadSequences();

        /// The rules, from `mai_rule` and `mai_rule_step`. Called ONCE, from
        /// LoadData's final phase -- never from a reload, because live AI
        /// objects point into the result.
        void LoadRules();

        std::unordered_map<Key, mai::Sequence, KeyHash> m_sequences;
        std::unordered_map<Map const*, std::vector<mai::Frame>> m_frames;

        /// The rules, per creature ENTRY -- because that is what they are the
        /// same for. Every Onyxia in the world has these rules; what differs
        /// between two of them is the timers and the phase, which live on the
        /// AI object rather than here.
        ///
        /// The AI objects hold pointers INTO this, so it must not be rebuilt
        /// while any of them exist. Loading happens once at start-up and
        /// reloading `creature_ai_scripts` is refused for exactly that reason.
        std::unordered_map<uint32, mai::RuleSet> m_rules;
    };
}

#endif //MANGOS_MAI_ENGINE_H
