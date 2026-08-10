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

#ifndef MANGOS_LUAU_ENGINE_H
#define MANGOS_LUAU_ENGINE_H

#include "IScriptEngine.h"

#include <bitset>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace scripting
{
    /**
     * Luau, the scripting engine an operator writes scripts in.
     *
     * The first engine here that is a LANGUAGE rather than a table format, and
     * the first that has no case per event: it is handed an id and an array of
     * Args and builds a Lua value out of the manifest's own description of
     * that event. Adding an event to the manifest makes it visible to scripts
     * with no edit here at all -- which is the return on having written the
     * manifest down in the first place.
     *
     * Three decisions worth stating, because none of them is arbitrary.
     *
     * ONE STATE PER MAP. Maps update in parallel in this core, and a lua_State
     * is not thread-safe, so a single shared VM would be a data race on every
     * event raised from two maps at once. Each map gets its own state, created
     * on first use and closed by RetireState when the map goes; global-scope
     * events get one more. Every state runs the same compiled bytecode, so
     * "the scripts" are one thing and only their live values are per-map.
     *
     * GUIDS ARE NOT NUMBERS. An ObjectGuid is 64 bits and a Lua number is a
     * double, which holds 53 -- passing one as a number would silently round
     * it, and the corruption would look like a script bug forever. They cross
     * as boxed userdata with a metatable, so they can be compared and printed
     * but not quietly mangled. Handles and borrows are boxed for the same
     * reason, and a borrow is checked against its epoch every time it is
     * touched, so a script that stores one and reads it next tick gets a
     * script error rather than freed memory.
     *
     * A HANDLER'S RETURN IS THE VERDICT. Returning nothing means Continue,
     * `false` means Cancel and `true` means Handled -- and asking for a
     * verdict the event does not offer is refused rather than ignored, so a
     * script cannot believe it cancelled something that was never cancellable.
     * In/out slots are not returns: the handler mutates the payload table and
     * the engine reads the fields back.
     *
     * A SCRIPT RUNS ON A BUDGET. pcall catches an error; it does not catch a
     * script that never finishes, and "nothing from a script may unwind
     * through the world tick" is worth nothing if a `while true do end` can
     * stop the tick instead. Every state therefore runs under a wall-clock
     * deadline enforced at the VM's own safepoints and a ceiling on the heap
     * it may own, both configurable and both able to be turned off by an
     * operator who would rather have the hang than the interruption.
     */
    class LuauEngine : public IEngine
    {
    public:
        LuauEngine();
        ~LuauEngine() override;

        char const* GetName() const override { return "Luau"; }

        Verdict Dispatch(Context const& ctx, EventId id, Arg* args,
                         std::size_t count) override;

        void LoadData(LoadPhase phase) override;
        bool ReloadData(char const* table) override;
        void RetireState(Context const& ctx) override;

        /**
         * What one state is allowed to spend.
         *
         * Reached from the VM through lua_callbacks()->userdata, so it must
         * outlive every allocation the state makes -- which is why it is a
         * member of State declared ahead of the lua_State, and why a State is
         * only ever held behind a unique_ptr: the allocator keeps this
         * address for the whole life of the VM, including the free that
         * lua_close performs.
         *
         * Public only because the allocator and the interrupt are plain C
         * callbacks the VM holds by pointer; nothing outside this engine has
         * any reason to name it.
         */
        struct Budget
        {
            /// The heap this state has taken, and the most it may take.
            /// Refusing an allocation is reported to the script as an ordinary
            /// out-of-memory error, which pcall catches like any other.
            std::size_t used = 0;
            std::size_t limit = 0;      ///< 0 means no ceiling

            /// How long one handler may run for. Checked at the VM's
            /// safepoints, but not at every one of them: a clock read on every
            /// loop back edge would cost more than the scripts do, so a
            /// countdown decides when to look.
            uint32 timeLimitMs = 0;     ///< 0 means no deadline
            uint32 stepsLeft = 0;
            bool   running = false;
            std::chrono::steady_clock::time_point deadline{};
        };

    private:
        /// One compiled script, kept as bytecode so every state shares the
        /// parse and only pays for its own closures.
        struct Chunk
        {
            std::string name;       ///< as reported in a traceback
            std::string bytecode;
        };

        /// A live VM plus what it has registered.
        struct State
        {
            Budget     budget;          ///< declared first; see Budget
            lua_State* L = nullptr;
            ~State();
        };

        State* StateFor(Context const& ctx);

        /// @a map is the map this state answers for, and nullptr for the
        /// world state. It is not decoration: it is how every guid a script
        /// holds is turned back into an object.
        std::unique_ptr<State> OpenState(Map* map) const;

        void Compile();
        void RecordSubscriptions(lua_State* L);

        std::vector<Chunk> m_chunks;

        /// Read once when the scripts are compiled, and handed to every state
        /// as it is opened.
        uint32 m_timeLimitMs = 0;
        std::size_t m_memoryLimit = 0;

        /// Which ids any script registered for. Read before anything else
        /// happens, and it is why an event nobody wants costs one bit test
        /// and no lock: the scripts are identical in every state, so what the
        /// first state registered is what all of them register.
        std::bitset<0x10000> m_subscribed;

        /// The states, and the lock that only their creation needs.
        mutable std::mutex m_statesLock;
        std::unordered_map<Map const*, std::unique_ptr<State>> m_mapStates;
        std::unique_ptr<State> m_globalState;
    };
}

#endif //MANGOS_LUAU_ENGINE_H
