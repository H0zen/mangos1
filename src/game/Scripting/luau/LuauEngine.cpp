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

#include "Config/Config.h"
#include "Log.h"
#include "Map.h"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scripting
{
    namespace
    {
        /// The registry key under which a state keeps its handler table.
        char const HANDLERS[] = "mangos.handlers";

        /// Metatable names for the three things that must not be numbers.
        char const MT_GUID[]   = "mangos.guid";
        char const MT_HANDLE[] = "mangos.handle";
        char const MT_BORROW[] = "mangos.borrow";

        struct BoxedHandle { Handle value; };
        struct BoxedBorrow { Borrow value; };

        /**
         * Push @a raw as an opaque guid.
         *
         * Not lua_pushnumber. A Lua number is a double and an ObjectGuid is 64
         * bits, so a guid above 2^53 would come back changed and every use of
         * it would be wrong in a way no error ever names.
         */
        void PushGuid(lua_State* L, uint64 raw)
        {
            if (!raw)
            {
                lua_pushnil(L);
                return;
            }

            void* box = lua_newuserdata(L, sizeof(uint64));
            std::memcpy(box, &raw, sizeof(raw));
            luaL_getmetatable(L, MT_GUID);
            lua_setmetatable(L, -2);
        }

        void PushHandle(lua_State* L, Handle const& handle)
        {
            if (handle.IsEmpty())
            {
                lua_pushnil(L);
                return;
            }

            void* box = lua_newuserdata(L, sizeof(BoxedHandle));
            new (box) BoxedHandle{ handle };
            luaL_getmetatable(L, MT_HANDLE);
            lua_setmetatable(L, -2);
        }

        void PushBorrow(lua_State* L, Borrow const& borrow)
        {
            if (borrow.IsEmpty())
            {
                lua_pushnil(L);
                return;
            }

            void* box = lua_newuserdata(L, sizeof(BoxedBorrow));
            new (box) BoxedBorrow{ borrow };
            luaL_getmetatable(L, MT_BORROW);
            lua_setmetatable(L, -2);
        }

        uint64 GuidOf(lua_State* L, int index)
        {
            void* box = lua_touserdata(L, index);
            if (!box)
            {
                return 0;
            }

            uint64 raw = 0;
            std::memcpy(&raw, box, sizeof(raw));
            return raw;
        }

        /// One slot, as a Lua value.
        void PushArg(lua_State* L, Arg const& arg)
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
                    PushGuid(L, arg.AsEntity().guid);
                    break;
                case Arg::Kind::Named:
                    PushHandle(L, arg.AsNamed());
                    break;
                case Arg::Kind::Lent:
                    PushBorrow(L, arg.AsLent());
                    break;
                case Arg::Kind::Text:
                    lua_pushstring(L, arg.AsText().c_str());
                    break;
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
         */
        bool ReadBack(lua_State* L, int index, Arg& arg)
        {
            switch (arg.GetKind())
            {
                case Arg::Kind::Signed:
                    if (!lua_isnumber(L, index)) { return false; }
                    arg = Arg::FromSigned(int64(lua_tonumber(L, index)));
                    return true;

                case Arg::Kind::Number:
                    if (!lua_isnumber(L, index)) { return false; }
                    arg = Arg::FromNumber(uint64(lua_tonumber(L, index)));
                    return true;

                case Arg::Kind::Real:
                    if (!lua_isnumber(L, index)) { return false; }
                    arg = Arg::FromReal(lua_tonumber(L, index));
                    return true;

                case Arg::Kind::Flag:
                    if (!lua_isboolean(L, index)) { return false; }
                    arg = Arg::FromFlag(lua_toboolean(L, index) != 0);
                    return true;

                case Arg::Kind::Text:
                    if (!lua_isstring(L, index)) { return false; }
                    // Written through the reference the caller gave us; the
                    // string lives in the emitting frame, not here.
                    arg.AsText().assign(lua_tostring(L, index));
                    return true;

                case Arg::Kind::Entity:
                    if (lua_isnil(L, index))
                    {
                        arg = Arg::FromEntity(Ref{ 0 });
                        return true;
                    }
                    if (!lua_isuserdata(L, index)) { return false; }
                    arg = Arg::FromEntity(Ref{ GuidOf(L, index) });
                    return true;

                default:
                    // A handle or a borrow is an identity the world owns. A
                    // script may read one and pass it on; it may not invent
                    // one, so these are never read back.
                    return true;
            }
        }

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

        int Lua_GuidToString(lua_State* L)
        {
            char text[24];
            std::snprintf(text, sizeof(text), "guid:%llu",
                          (unsigned long long)GuidOf(L, 1));
            lua_pushstring(L, text);
            return 1;
        }

        int Lua_GuidEquals(lua_State* L)
        {
            lua_pushboolean(L, GuidOf(L, 1) == GuidOf(L, 2) ? 1 : 0);
            return 1;
        }

        /// A borrow answers one question honestly: am I still usable?
        int Lua_BorrowAlive(lua_State* L)
        {
            BoxedBorrow const* box =
                static_cast<BoxedBorrow const*>(lua_touserdata(L, 1));
            lua_pushboolean(L,
                (box && detail::IsBorrowLive(box->value)) ? 1 : 0);
            return 1;
        }

        void RegisterMetatable(lua_State* L, char const* name,
                               lua_CFunction tostring, lua_CFunction eq)
        {
            luaL_newmetatable(L, name);
            if (tostring)
            {
                lua_pushcfunction(L, tostring, "__tostring");
                lua_setfield(L, -2, "__tostring");
            }
            if (eq)
            {
                lua_pushcfunction(L, eq, "__eq");
                lua_setfield(L, -2, "__eq");
            }
            // Opaque: a script may hold one and compare it, and that is all.
            lua_pushstring(L, name);
            lua_setfield(L, -2, "__metatable");
            lua_pop(L, 1);
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

    std::unique_ptr<LuauEngine::State> LuauEngine::OpenState() const
    {
        std::unique_ptr<State> state(new State());
        state->L = luaL_newstate();
        if (!state->L)
        {
            return nullptr;
        }

        lua_State* L = state->L;
        luaL_openlibs(L);

        RegisterMetatable(L, MT_GUID, Lua_GuidToString, Lua_GuidEquals);
        RegisterMetatable(L, MT_HANDLE, nullptr, nullptr);
        RegisterMetatable(L, MT_BORROW, nullptr, nullptr);

        // IsLive() on a borrow, so a script can ask instead of guessing.
        luaL_getmetatable(L, MT_BORROW);
        lua_newtable(L);
        lua_pushcfunction(L, Lua_BorrowAlive, "IsLive");
        lua_setfield(L, -2, "IsLive");
        lua_setfield(L, -2, "__index");
        lua_pop(L, 1);

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

            if (lua_pcall(thread, 0, 0, 0) != LUA_OK)
            {
                sLog.outError("Luau: %s failed to run: %s",
                              chunk.name.c_str() + 1, lua_tostring(thread, -1));
            }

            lua_pop(L, 1);      // the thread
        }

        return state;
    }

    void LuauEngine::Compile()
    {
        m_chunks.clear();

        std::string const path =
            sConfig.GetStringDefault("Luau.ScriptsPath", "scripts/luau");

        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec))
        {
            sLog.outString("Luau: no script directory at '%s'; none loaded.",
                           path.c_str());
            return;
        }

        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;          // keep line numbers in tracebacks

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

            std::ifstream file(it->path(), std::ios::binary);
            if (!file)
            {
                sLog.outError("Luau: cannot read '%s'.",
                              it->path().string().c_str());
                continue;
            }

            std::ostringstream text;
            text << file.rdbuf();

            Chunk chunk;
            chunk.name = "=" + it->path().filename().string();
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
        std::unique_ptr<State> global = OpenState();
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

        // This throws away every live state, which is only safe because of
        // WHERE it runs: World::ProcessCliCommands is called from
        // World::Update, and the parallel map update in UpdateSimulation has
        // already joined by then. No map thread can be inside a VM here. If a
        // reload ever became reachable from a map thread this would be a use
        // after free, so it is worth saying out loud rather than trusting.
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
        std::lock_guard<std::mutex> guard(m_statesLock);

        auto found = m_mapStates.find(ctx.map);
        if (found != m_mapStates.end())
        {
            return found->second.get();
        }

        std::unique_ptr<State> state = OpenState();
        if (!state)
        {
            return nullptr;
        }

        State* raw = state.get();
        m_mapStates.emplace(ctx.map, std::move(state));
        return raw;
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

        for (int i = 1; i <= total; ++i)
        {
            lua_rawgeti(L, handlers, i);

            // The payload, by the manifest's own field names.
            lua_newtable(L);
            for (std::size_t slot = 0; slot < count; ++slot)
            {
                PushArg(L, args[slot]);
                lua_setfield(L, -2, spec->args[slot].name);
            }

            // Pushed twice on purpose. lua_pcall consumes the function and
            // its arguments, so the copy it eats is not the one the in/out
            // slots are read back out of afterwards.
            lua_pushvalue(L, -1);

            if (lua_pcall(L, 1, 1, 0) != LUA_OK)
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
