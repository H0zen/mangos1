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

#include "LuauEngine.h"

#ifdef ENABLE_LUAU

#include "api/LuaApi.h"

#include "Config/Config.h"
#include "Log.h"
#include "Map.h"
#include "ObjectGuid.h"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace scripting
{
    namespace
    {
        /// The registry key under which a state keeps its handler table.
        char const HANDLERS[] = "mangos.handlers";

        /// How many VM safepoints pass between two reads of the clock. The
        /// deadline is therefore approximate by whatever a script can execute
        /// in this many safepoints, which is microseconds, and the check costs
        /// a decrement rather than a clock read on the other 4095.
        uint32 const CLOCK_STRIDE = 4096;

        /**
         * One slot, as a Lua value.
         *
         * The boxes themselves belong to the API layer, because that is what
         * gives them methods: a guid's metatable carries both the equality
         * this engine needs and the __index the API installs, and two files
         * each creating "the" guid metatable would be two metatables.
         *
         * @a owner is the guid of the player who owns whatever item is in
         * this payload, and 0 elsewhere. An item is the one object in this
         * core with no global registry -- only Player::GetItemByGuid -- so an
         * item guid alone is not resolvable, and the box carries the owner
         * that makes it so. A payload with no player in it yields 0, and the
         * item in it is then only usable by the events that do carry one.
         */
        void PushArg(lua_State* L, Arg const& arg, uint64 owner)
        {
            switch (arg.GetKind())
            {
                case Arg::Kind::Signed:
                    lua_pushnumber(L, double(arg.AsSigned()));
                    break;
                case Arg::Kind::Number:
                    lua_pushnumber(L, double(arg.AsNumber()));
                    break;
                case Arg::Kind::Real:
                    lua_pushnumber(L, arg.AsReal());
                    break;
                case Arg::Kind::Flag:
                    lua_pushboolean(L, arg.AsFlag() ? 1 : 0);
                    break;
                case Arg::Kind::Entity:
                {
                    uint64 const raw = arg.AsEntity().guid;
                    bool const isItem =
                        ObjectGuid(raw).GetHigh() == HIGHGUID_ITEM;
                    api::PushOwnedGuid(L, raw, isItem ? owner : 0);
                    break;
                }
                case Arg::Kind::Named:
                    api::PushRawHandle(L, arg.AsNamed());
                    break;
                case Arg::Kind::Lent:
                    api::PushRawBorrow(L, arg.AsLent());
                    break;
                case Arg::Kind::Text:
                {
                    // Length, not strlen: a payload string is whatever the
                    // world put in it, and a chat line or an addon message may
                    // legitimately carry an embedded NUL. Pushing it as a C
                    // string would hand the script a silently truncated copy
                    // and then write the truncation back through ReadBack.
                    std::string const& text = arg.AsText();
                    lua_pushlstring(L, text.c_str(), text.size());
                    break;
                }
                case Arg::Kind::Empty:
                default:
                    lua_pushnil(L);
                    break;
            }
        }

        /**
         * Read one in/out slot back out of the payload table.
         *
         * The Kind an argument arrived with is the Kind it must leave with --
         * that is the seam's rule, not this engine's -- so a script that puts
         * a string where a number was is refused here rather than allowed to
         * change the shape of the payload underneath the caller.
         *
         * The tests are on lua_type and not on lua_isnumber/lua_isstring, and
         * the difference is the whole rule: those two answer "would coerce",
         * so `lua_isnumber` is true of the string "10" and `lua_isstring` is
         * true of the number 5. Written with them, this function accepted
         * exactly what its own comment says it refuses.
         */
        bool ReadBack(lua_State* L, int index, Arg& arg)
        {
            switch (arg.GetKind())
            {
                case Arg::Kind::Signed:
                    if (lua_type(L, index) != LUA_TNUMBER) { return false; }
                    arg = Arg::FromSigned(int64(lua_tonumber(L, index)));
                    return true;

                case Arg::Kind::Number:
                {
                    if (lua_type(L, index) != LUA_TNUMBER) { return false; }

                    // A Lua number is signed and this slot is not. Casting a
                    // negative one straight to uint64 turns -1 into the
                    // largest number there is, which then arrives at the call
                    // site as a perfectly plausible amount.
                    double const value = lua_tonumber(L, index);
                    if (!(value >= 0.0)) { return false; }
                    arg = Arg::FromNumber(uint64(value));
                    return true;
                }

                case Arg::Kind::Real:
                    if (lua_type(L, index) != LUA_TNUMBER) { return false; }
                    arg = Arg::FromReal(lua_tonumber(L, index));
                    return true;

                case Arg::Kind::Flag:
                    if (lua_type(L, index) != LUA_TBOOLEAN) { return false; }
                    arg = Arg::FromFlag(lua_toboolean(L, index) != 0);
                    return true;

                case Arg::Kind::Text:
                {
                    if (lua_type(L, index) != LUA_TSTRING) { return false; }

                    // Written through the reference the caller gave us; the
                    // string lives in the emitting frame, not here. Read with
                    // its length, so what comes back is what the script set
                    // rather than what fits before the first NUL.
                    std::size_t length = 0;
                    char const* text = lua_tolstring(L, index, &length);
                    arg.AsText().assign(text, length);
                    return true;
                }

                case Arg::Kind::Entity:
                {
                    if (lua_isnil(L, index))
                    {
                        arg = Arg::FromEntity(Ref{ 0 });
                        return true;
                    }

                    // A guid box and nothing else. RawGuidAt answers 0 for a
                    // handle or a borrow as readily as for a guid that is
                    // genuinely empty, so taking its answer unexamined turned
                    // "you put the wrong kind of box here" into "the slot is
                    // now empty" -- a report the call site cannot tell from a
                    // deliberate clearing.
                    uint64 const raw = api::RawGuidAt(L, index);
                    if (!raw) { return false; }
                    arg = Arg::FromEntity(Ref{ raw });
                    return true;
                }

                default:
                    // A handle or a borrow is an identity the world owns. A
                    // script may read one and pass it on; it may not invent
                    // one, so these are never read back.
                    return true;
            }
        }

        /**
         * The message handler every pcall runs under.
         *
         * Compile() asks for line numbers; without a handler installed here
         * nothing ever asked for the stack they belong to, so an error in a
         * function three calls deep reported only the innermost line and left
         * the reader to guess which handler it came from.
         */
        int Lua_Traceback(lua_State* L)
        {
            char const* message = lua_tostring(L, 1);
            luaL_traceback(L, L, message ? message : "(non-string error)", 1);
            return 1;
        }

        /**
         * The heap this state may own, counted as it is handed out.
         *
         * Refusing an allocation is how a VM is told it is out of memory; the
         * error that follows is an ordinary Lua error and pcall catches it
         * like any other. That is the difference between a script that
         * allocates without end costing one state its work and costing the
         * process its life.
         *
         * The Lua allocator contract is inherited from 5.1 and has one trap:
         * @a osize is the SIZE of the old block only when @a ptr is non-null.
         * For a fresh allocation it carries the type of the object being made,
         * so reading it there would subtract a tag from a byte count.
         */
        void* Reallocate(void* ud, void* ptr, std::size_t osize,
                         std::size_t nsize)
        {
            LuauEngine::Budget* budget = static_cast<LuauEngine::Budget*>(ud);
            std::size_t const oldSize = ptr ? osize : 0;

            if (nsize == 0)
            {
                budget->used -= oldSize;
                std::free(ptr);
                return nullptr;
            }

            if (budget->limit && nsize > oldSize &&
                budget->used + (nsize - oldSize) > budget->limit)
            {
                return nullptr;
            }

            void* block = std::realloc(ptr, nsize);
            if (!block)
            {
                return nullptr;
            }

            budget->used = budget->used - oldSize + nsize;
            return block;
        }

        /**
         * The deadline, enforced where the VM lets it be enforced.
         *
         * @a gc is -1 at an ordinary safepoint -- a loop back edge, a call, a
         * return -- and the VM protects those, so raising an error is legal
         * there. Anything else is a garbage-collection state, where an error
         * would unwind the collector, so it is left alone.
         */
        void Interrupt(lua_State* L, int gc)
        {
            if (gc >= 0)
            {
                return;
            }

            LuauEngine::Budget* budget = static_cast<LuauEngine::Budget*>(
                lua_callbacks(L)->userdata);
            if (!budget || !budget->running)
            {
                return;
            }

            if (--budget->stepsLeft != 0)
            {
                return;
            }
            budget->stepsLeft = CLOCK_STRIDE;

            if (std::chrono::steady_clock::now() < budget->deadline)
            {
                return;
            }

            // Disarmed before the throw, and it matters: the unwinding itself
            // runs through safepoints, and a deadline that is still expired
            // would fire again inside its own error handling.
            budget->running = false;
            luaL_error(L, "this handler ran for longer than %u ms and was "
                          "stopped; the world does not wait for a script",
                       budget->timeLimitMs);
        }

        /**
         * Arm the deadline for one entry into a state, and disarm it after.
         *
         * A handler can cause the world to raise another event, which lands
         * back in this same state. The inner entry does NOT get a fresh
         * deadline: the outer one is still the honest limit on how long the
         * world has been waiting, and refreshing it would let a script that
         * emits events in a loop run for ever, one budget at a time.
         */
        struct BudgetGuard
        {
            LuauEngine::Budget& budget;
            bool const wasRunning;
            std::chrono::steady_clock::time_point const wasDeadline;

            explicit BudgetGuard(LuauEngine::Budget& b)
                : budget(b), wasRunning(b.running), wasDeadline(b.deadline)
            {
                if (budget.running || budget.timeLimitMs == 0)
                {
                    return;
                }

                budget.stepsLeft = CLOCK_STRIDE;
                budget.deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(budget.timeLimitMs);
                budget.running = true;
            }

            ~BudgetGuard()
            {
                budget.running = wasRunning;
                budget.deadline = wasDeadline;
            }
        };

        /// `OnEvent(id, handler)` -- the whole registration surface.
        int Lua_OnEvent(lua_State* L)
        {
            int const id = int(luaL_checkinteger(L, 1));
            luaL_checktype(L, 2, LUA_TFUNCTION);

            if (id <= 0 || id > 0xFFFF)
            {
                luaL_error(L, "OnEvent: %d is not an event id", id);
            }

            if (!SpecOf(EventId(id)))
            {
                luaL_error(L, "OnEvent: no event carries id 0x%04X", id);
            }

            lua_getfield(L, LUA_REGISTRYINDEX, HANDLERS);
            lua_rawgeti(L, -1, id);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_rawseti(L, -3, id);
            }

            // handlers[id][#handlers[id] + 1] = fn
            int const next = int(lua_objlen(L, -1)) + 1;
            lua_pushvalue(L, 2);
            lua_rawseti(L, -2, next);
            lua_pop(L, 2);
            return 0;
        }

    }

    LuauEngine::State::~State()
    {
        if (L)
        {
            lua_close(L);
        }
    }

    LuauEngine::LuauEngine() = default;
    LuauEngine::~LuauEngine() = default;

    std::unique_ptr<LuauEngine::State> LuauEngine::OpenState(Map* map) const
    {
        std::unique_ptr<State> state(new State());
        state->budget.limit = m_memoryLimit;
        state->budget.timeLimitMs = m_timeLimitMs;

        // lua_newstate rather than luaL_newstate, which is the same call with
        // the library's own allocator: this one counts, and can say no.
        state->L = lua_newstate(&Reallocate, &state->budget);
        if (!state->L)
        {
            return nullptr;
        }

        lua_State* L = state->L;

        // Set before anything runs, including the standard libraries. The
        // callbacks live on the global state, so every thread this state ever
        // spawns is covered by the one assignment.
        lua_callbacks(L)->userdata = &state->budget;
        lua_callbacks(L)->interrupt = &Interrupt;

        luaL_openlibs(L);

        // The world API: the boxes, their metatables, every method a script
        // can call and the globals. Everything below this line assumes it is
        // already there.
        api::RegisterApi(L, map);

        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, HANDLERS);

        lua_pushcfunction(L, Lua_OnEvent, "OnEvent");
        lua_setglobal(L, "OnEvent");

        // Every event id the manifest knows, by name, so a script says
        // OnEvent(EVENT.player_on_login, ...) rather than a bare number.
        lua_newtable(L);
        for (EventSpec const& spec : g_eventSpecs)
        {
            std::string name(spec.name);
            for (char& c : name)
            {
                if (c == '.')
                {
                    c = '_';
                }
            }

            lua_pushinteger(L, int(spec.id));
            lua_setfield(L, -2, name.c_str());
        }
        lua_setglobal(L, "EVENT");

        // Luau's own sandbox: the globals are frozen, so nothing a script does
        // can reach back and change the host's table. It has to happen AFTER
        // the API above is installed and BEFORE any script runs, which leaves
        // exactly one place for it.
        luaL_sandbox(L);

        // Each script then runs on its own sandboxed thread, which gives it an
        // environment that inherits the globals but cannot write through to
        // them. Two scripts declaring the same global do not collide, and
        // neither can redefine OnEvent for the other.
        //
        // Under the same budget as a handler, and for a stronger reason: a
        // top-level loop that never ends would hang whichever thread first
        // touched this map, and at start-up that is the world thread.
        //
        // Marked as loading throughout, which is what stops a script from
        // asking a question whose answer differs between states -- see the
        // note on IsLoading.
        api::SetLoading(L, true);

        for (Chunk const& chunk : m_chunks)
        {
            lua_State* thread = lua_newthread(L);
            luaL_sandboxthread(thread);

            if (luau_load(thread, chunk.name.c_str(), chunk.bytecode.data(),
                          chunk.bytecode.size(), 0) != 0)
            {
                sLog.outError("Luau: %s failed to load: %s",
                              chunk.name.c_str() + 1, lua_tostring(thread, -1));
                lua_pop(L, 1);
                continue;
            }

            {
                BudgetGuard guard(state->budget);
                if (lua_pcall(thread, 0, 0, 0) != LUA_OK)
                {
                    sLog.outError("Luau: %s failed to run: %s",
                                  chunk.name.c_str() + 1,
                                  lua_tostring(thread, -1));
                }
            }

            lua_pop(L, 1);      // the thread
        }

        api::SetLoading(L, false);
        return state;
    }

    void LuauEngine::Compile()
    {
        m_chunks.clear();

        std::string const path =
            sConfig.GetStringDefault("Luau.ScriptsPath", "scripts/luau");

        // Read here rather than in OpenState: a map's state is opened on that
        // map's own thread, and sConfig is not something to be read from four
        // of them at once. Compile runs on the world thread, alone.
        m_timeLimitMs = sConfig.GetIntDefault("Luau.HandlerTimeMs", 200);
        m_memoryLimit = std::size_t(
            sConfig.GetIntDefault("Luau.MemoryLimitMb", 64)) * 1024u * 1024u;

        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec))
        {
            sLog.outString("Luau: no script directory at '%s'; none loaded.",
                           path.c_str());
            return;
        }

        // Collected, then SORTED, and the sort is not tidiness. Handlers run
        // in registration order and the chain stops at the first Cancel or
        // Handled, so the order the scripts load in decides which one gets to
        // veto an action. Left to the directory iterator that order is the
        // file system's -- different on NTFS and ext4, and changed by renaming
        // an unrelated file.
        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(path, ec), end;
             it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file())
            {
                continue;
            }

            std::string const ext = it->path().extension().string();
            if (ext != ".luau" && ext != ".lua")
            {
                continue;
            }

            files.push_back(it->path());
        }

        std::sort(files.begin(), files.end());

        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;          // keep line numbers in tracebacks

        for (std::filesystem::path const& file : files)
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                sLog.outError("Luau: cannot read '%s'.",
                              file.string().c_str());
                continue;
            }

            std::ostringstream text;
            text << stream.rdbuf();

            // The path from the scripts root, not the bare file name: two
            // directories may each hold a handlers.luau, and a traceback that
            // names only the second half of the path names neither of them.
            std::error_code pathError;
            std::filesystem::path const shown =
                std::filesystem::relative(file, path, pathError);

            Chunk chunk;
            chunk.name = "=" + (pathError ? file.generic_string()
                                          : shown.generic_string());
            chunk.bytecode = Luau::compile(text.str(), options);
            m_chunks.push_back(std::move(chunk));
        }

        sLog.outString("Luau: compiled %u script(s) from '%s'.",
                       uint32(m_chunks.size()), path.c_str());
    }

    void LuauEngine::RecordSubscriptions(lua_State* L)
    {
        m_subscribed.reset();

        lua_getfield(L, LUA_REGISTRYINDEX, HANDLERS);
        lua_pushnil(L);
        while (lua_next(L, -2))
        {
            if (lua_isnumber(L, -2))
            {
                int const id = int(lua_tointeger(L, -2));
                if (id > 0 && id <= 0xFFFF)
                {
                    m_subscribed.set(std::size_t(id));
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    void LuauEngine::LoadData(LoadPhase phase)
    {
        if (phase != LoadPhase::Final)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> guard(m_statesLock);
            m_mapStates.clear();
            m_globalState.reset();
        }

        Compile();
        if (m_chunks.empty())
        {
            m_subscribed.reset();
            return;
        }

        // The global state is built eagerly and doubles as the census: every
        // state runs the same bytecode, so whatever this one registers is
        // what all of them will, and Dispatch can then reject an unwanted
        // event without touching a state or a lock.
        //
        // That premise is not merely hoped for -- the one value a script could
        // have branched on at load time, the bound map, refuses to answer
        // while loading. See api::IsLoading.
        std::unique_ptr<State> global = OpenState(nullptr);
        if (!global)
        {
            sLog.outError("Luau: could not create the script state.");
            return;
        }

        RecordSubscriptions(global->L);

        std::lock_guard<std::mutex> guard(m_statesLock);
        m_globalState = std::move(global);
    }

    bool LuauEngine::ReloadData(char const* table)
    {
        // Not a database table, and deliberately named like one anyway: an
        // administrator reloading scripts is doing the same thing as an
        // administrator reloading a table, and there is one command for it.
        if (std::strcmp(table, "luau") != 0)
        {
            return false;
        }

        // This throws away every live state, so the invariant it rests on is
        // worth stating rather than trusting: NO MAP THREAD MAY BE INSIDE A VM
        // WHEN THIS RUNS, or it is a use after free of the state that thread
        // is executing.
        //
        // Both routes in satisfy it today, and for different reasons. The
        // console goes through World::ProcessCliCommands, which World::Update
        // calls after UpdateSimulation has joined the parallel map update. A
        // GM typing `.reload luau` goes through CMSG_MESSAGECHAT, which the
        // opcode table marks PROCESS_THREADUNSAFE -- so it is handled by
        // World::UpdateSessions, on the world thread, BEFORE sMapMgr.Update
        // starts any of them.
        //
        // The second is the fragile one: mark that opcode as map-safe and this
        // becomes a crash with no other change anywhere.
        LoadData(LoadPhase::Final);
        return true;
    }

    void LuauEngine::RetireState(Context const& ctx)
    {
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return;
        }

        std::lock_guard<std::mutex> guard(m_statesLock);
        m_mapStates.erase(ctx.map);
    }

    LuauEngine::State* LuauEngine::StateFor(Context const& ctx)
    {
        if (ctx.scope == Context::Scope::Global)
        {
            return m_globalState.get();
        }

        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return nullptr;
        }

        // The lock guards the CONTAINER, not the state it hands back, and it
        // does not need to guard the state: a map is updated by one thread at
        // a time and is retired by the thread that owned it, so the only
        // thread that can be dispatching into a map's VM is the one that
        // would also be closing it. What two threads really do at once is
        // insert two different maps, which is what this serialises.
        {
            std::lock_guard<std::mutex> guard(m_statesLock);

            auto found = m_mapStates.find(ctx.map);
            if (found != m_mapStates.end())
            {
                return found->second.get();
            }
        }

        // Built with the lock RELEASED. OpenState runs the top level of every
        // script, which is code an operator wrote and which may call back into
        // the world; a call into the world may raise an event, and that event
        // arrives here again. Holding a non-recursive mutex across that is a
        // deadlock waiting for the first script that does anything at load
        // time -- and the cost of not holding it is only that two entries
        // could build the same map's state, which the emplace below settles.
        std::unique_ptr<State> state = OpenState(ctx.map);
        if (!state)
        {
            return nullptr;
        }

        std::lock_guard<std::mutex> guard(m_statesLock);

        // emplace, not insert_or_assign: whichever build finished first is the
        // one every borrow and every registered handler already belongs to, so
        // the loser is the one that gets dropped.
        return m_mapStates.emplace(ctx.map, std::move(state))
                   .first->second.get();
    }

    Verdict LuauEngine::Dispatch(Context const& ctx, EventId id, Arg* args,
                                 std::size_t count)
    {
        // One bit test, no lock, no state. Almost every event raised in a
        // running world lands here and goes no further.
        if (!m_subscribed.test(std::size_t(id)))
        {
            return Verdict::Continue;
        }

        EventSpec const* spec = SpecOf(id);
        if (!spec || spec->arity != count)
        {
            return Verdict::Continue;
        }

        State* state = StateFor(ctx);
        if (!state || !state->L)
        {
            return Verdict::Continue;
        }

        lua_State* L = state->L;
        int const base = lua_gettop(L);
        Verdict verdict = Verdict::Continue;

        lua_getfield(L, LUA_REGISTRYINDEX, HANDLERS);
        lua_rawgeti(L, -1, int(id));
        if (lua_isnil(L, -1))
        {
            lua_settop(L, base);
            return Verdict::Continue;
        }

        int const handlers = lua_gettop(L);
        int const total = int(lua_objlen(L, handlers));

        // Installed once for the whole loop and left at a known index, so the
        // error a handler raises arrives with the stack it was raised from
        // rather than one line and no context.
        lua_pushcfunction(L, Lua_Traceback, "traceback");
        int const traceback = lua_gettop(L);

        // The owner an item in this payload needs to be findable again: an
        // item is the one object in this core with no global registry, so its
        // box carries the player whose bags it is in.
        //
        // Found by what a guid IS rather than by what the manifest happened to
        // call the slot. Keying on the name "player" looked exact and was not:
        // item.on_dummy_effect and core.on_effect_dummy both carry a target
        // that may be an item and neither names a player at all, and nothing
        // in gen_events.py ever enforced the pairing that the name assumed.
        uint64 owner = 0;
        for (std::size_t slot = 0; slot < count; ++slot)
        {
            if (args[slot].GetKind() != Arg::Kind::Entity)
            {
                continue;
            }

            uint64 const raw = args[slot].AsEntity().guid;
            if (raw && ObjectGuid(raw).IsPlayer())
            {
                owner = raw;
                break;
            }
        }

        for (int i = 1; i <= total; ++i)
        {
            // The payload FIRST, then the handler, then the copy that gets
            // eaten. lua_pcall takes its function from top-(nargs+1), so with
            // the two pushes the other way round the table below the argument
            // was what got called -- every handler failed with "attempt to
            // call a table value", and the pops below happened to keep the
            // stack balanced, so nothing but the log ever said so.
            lua_newtable(L);
            for (std::size_t slot = 0; slot < count; ++slot)
            {
                PushArg(L, args[slot], owner);
                lua_setfield(L, -2, spec->args[slot].name);
            }

            lua_rawgeti(L, handlers, i);
            lua_pushvalue(L, -2);       // the payload, as the one argument

            int status;
            {
                BudgetGuard guard(state->budget);
                status = lua_pcall(L, 1, 1, traceback);
            }

            if (status != LUA_OK)
            {
                // An engine swallows its own failures: one script's mistake
                // must not unwind through a world tick, and the next handler
                // still gets its turn.
                sLog.outError("Luau: %s in %s", lua_tostring(L, -1),
                              spec->name);
                lua_pop(L, 2);      // the message and the payload
                continue;
            }

            if (lua_isboolean(L, -1))
            {
                bool const answer = lua_toboolean(L, -1) != 0;
                if (!answer && spec->cancellable)
                {
                    verdict = Verdict::Cancel;
                }
                else if (answer && spec->claimable)
                {
                    verdict = Verdict::Handled;
                }
                else
                {
                    // Saying no to something that cannot be refused, or
                    // claiming something that cannot be claimed. Reported,
                    // because a script that believes it stopped an action it
                    // never could is a bug that otherwise never surfaces.
                    sLog.outError(
                        "Luau: %s answered %s, which %s does not accept",
                        spec->name, answer ? "true" : "false", spec->name);
                }
            }

            lua_pop(L, 1);      // the result

            // In/out slots come back out of the table the handler mutated,
            // which is the copy left under the result we just popped.
            for (std::size_t slot = 0; slot < count; ++slot)
            {
                if (!spec->args[slot].inout)
                {
                    continue;
                }

                lua_getfield(L, -1, spec->args[slot].name);
                if (!ReadBack(L, -1, args[slot]))
                {
                    sLog.outError("Luau: %s wrote the wrong kind of value to "
                                  "%s.%s", spec->name, spec->name,
                                  spec->args[slot].name);
                }
                lua_pop(L, 1);
            }

            lua_pop(L, 1);      // the payload table

            if (verdict != Verdict::Continue)
            {
                break;
            }
        }

        lua_settop(L, base);
        return verdict;
    }
}

#endif //ENABLE_LUAU
