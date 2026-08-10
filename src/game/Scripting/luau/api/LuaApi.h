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
 *
 * The method surface this exposes is a port of the Eluna Lua Engine's, which
 * is GPL-3.0 and was carried in this repository as src/modules/Eluna until it
 * was removed. Eluna spent fifteen years deciding WHICH methods a script
 * author actually wants and what to call them; that judgement is worth far
 * more than the code, and re-deriving it would have been vanity. The bodies
 * are rewritten against this tree's API -- Placement for everything spatial,
 * terrain::Column for everything about the ground -- but the names, the
 * argument orders and the documentation are Eluna's, and a script written
 * against Eluna's manual will mostly read correctly here.
 */

#ifndef MANGOS_LUAU_API_H
#define MANGOS_LUAU_API_H

#include "Platform/Define.h"
#include "ScriptTypes.h"

#include <string>

struct lua_State;

class ObjectGuid;
class Aura;
class BattleGround;
class Corpse;
class Creature;
class GameObject;
class Group;
class Guild;
class Item;
class Map;
class Object;
class Player;
class Spell;
class Unit;
class WorldObject;
class WorldPacket;
struct Quest;

namespace scripting
{
    namespace api
    {
        /**
         * WHAT A RECEIVER IS, and therefore which methods answer for it.
         *
         * Kept as a flat enum rather than derived from C++ types because the
         * flattening happens once, at state creation: the table registered
         * for Player already contains Object's, WorldObject's and Unit's
         * methods, so a call costs one rawget and never walks a chain.
         */
        enum class TypeClass : uint8
        {
            None = 0,
            Object,
            WorldObject,
            Unit,
            Player,
            Creature,
            GameObject,
            Item,
            Corpse,
            Map,
            Group,
            Guild,
            Quest,
            Spell,
            Aura,
            Packet,
            BattleGround,
            Count
        };

        /**
         * The world as one script call sees it.
         *
         * Named for what it is rather than for the engine, because a method
         * body should read as a statement about the game and not about Lua.
         * It carries the two things every method needs and nothing else: the
         * stack the arguments are on, and the map the call happened in.
         *
         * THE BOUND MAP IS NOT DECORATION. Every object-valued argument
         * crosses as a guid, and a guid is resolved through this map at the
         * moment it is used. That is the whole reason a script cannot hold a
         * pointer to a dead creature: there is no pointer to hold.
         */
        class Api
        {
            public:
                Api(lua_State* state, Map* map) : L(state), m_map(map) {}

                lua_State* const L;

                Map* BoundMap() const { return m_map; }

                /// The map's id, or -1 in the world state. Scripts see this
                /// through a global; methods use it for messages.
                int32 BoundMapId() const;

                // ---- values out ------------------------------------------

                void Push();
                void Push(bool value);
                void Push(int8 value)   { PushSigned(value); }
                void Push(int16 value)  { PushSigned(value); }
                void Push(int32 value)  { PushSigned(value); }
                void Push(int64 value)  { PushSigned(value); }
                void Push(uint8 value)  { PushUnsigned(value); }
                void Push(uint16 value) { PushUnsigned(value); }
                void Push(uint32 value) { PushUnsigned(value); }
                void Push(uint64 value) { PushUnsigned(value); }
                void Push(float value);
                void Push(double value);
                void Push(char const* value);
                void Push(std::string const& value);

                /**
                 * An object, as the guid that names it.
                 *
                 * NOT as a pointer. This is the single decision the whole
                 * layer rests on: what a script holds is an identity, so the
                 * object it names may die between one call and the next
                 * without anything the script holds becoming dangerous.
                 */
                void Push(ObjectGuid guid);
                void Push(Object const* obj);
                void Push(WorldObject const* obj);
                void Push(Unit const* unit);
                void Push(Player const* player);
                void Push(Creature const* creature);
                void Push(GameObject const* go);
                void Push(Item const* item);
                void Push(Corpse const* corpse);

                /// Stable identities that are not guids.
                void Push(Map const* map);
                void Push(Group const* group);
                void Push(Guild const* guild);
                void Push(Quest const* quest);
                void Push(BattleGround const* bg);

                /// Things with no identity at all -- valid for this call only.
                void Push(Spell const* spell);
                void Push(Aura const* aura);
                void Push(WorldPacket const* packet);

                // ---- values in -------------------------------------------

                /// Reads argument @a narg, raising a Lua error if it is not
                /// the type asked for. Index 1 is the receiver, so a method's
                /// first real argument is 2 -- which is why every ported body
                /// starts counting there.
                template <typename T> T Check(int narg);

                /// The same, but an absent or nil argument yields @a def.
                template <typename T> T Check(int narg, T def)
                {
                    return IsNoneOrNil(narg) ? def : Check<T>(narg);
                }

                /**
                 * Resolve argument @a narg to a live object.
                 *
                 * Returns nullptr when the guid names something that is no
                 * longer there, which is a legitimate answer and not an
                 * error: a script may perfectly well ask about a creature
                 * that has since despawned. Pass error = true to make it a
                 * Lua error instead, for the arguments a method cannot work
                 * without.
                 */
                template <typename T> T* CheckObj(int narg, bool error = true);

                bool IsNoneOrNil(int narg) const;

                /// Resolve a guid through the bound map. Players are also
                /// findable without one, which is why a login handler in the
                /// world state can still ask a player its name.
                WorldObject* Resolve(ObjectGuid guid) const;

            private:
                void PushSigned(int64 value);
                void PushUnsigned(uint64 value);

                Map* m_map;
        };

        /**
         * One method, as the table that registers it sees it.
         *
         * The receiver arrives erased because a single thunk serves every
         * method on every type; the BIND macro each API file defines locally
         * restores the type with a capture-less lambda, so no method body
         * ever sees a void*.
         *
         * A null fn is not a mistake. It registers the NAME with a closure
         * that reports "not implemented in this core", which is what a script
         * ported from another emulator deserves instead of "attempt to call a
         * nil value" four frames away from the cause.
         */
        struct MethodEntry
        {
            char const* name;
            int (*fn)(Api&, void*);
        };

        /// Install everything on a fresh state. Must run before the sandbox
        /// closes the globals, and therefore has exactly one call site.
        void RegisterApi(lua_State* L, Map* map);

        /// The bound map, recovered from inside a C function that was only
        /// given a lua_State.
        Map* BoundMapOf(lua_State* L);

        /**
         * Whether this state is still running its scripts' top level.
         *
         * The engine keeps ONE census of which events anybody registered for,
         * taken from the world state, on the premise that every state runs the
         * same bytecode and therefore registers the same handlers. Exactly one
         * thing could falsify that premise -- a script branching at load time
         * on the only value that differs between states, which is the bound
         * map -- so during load there is no bound map to branch on and asking
         * for one is an error rather than a silent nil.
         */
        void SetLoading(lua_State* L, bool loading);
        bool IsLoading(lua_State* L);

        // ---- the three boxes ---------------------------------------------
        //
        // Owned here rather than by the engine because the API layer is what
        // gives them methods: a guid's metatable has to carry both the
        // engine's __eq and this layer's __index, and two files creating
        // "the" guid metatable would be two metatables.

        extern char const MT_GUID[];
        extern char const MT_HANDLE[];
        extern char const MT_BORROW[];

        void PushRawGuid(lua_State* L, uint64 raw);

        /// A guid that carries the owner it needs to be found again by. Only
        /// an item needs this; see the note on GuidBox.
        void PushOwnedGuid(lua_State* L, uint64 raw, uint64 owner);

        void PushRawHandle(lua_State* L, Handle const& handle);
        void PushRawBorrow(lua_State* L, Borrow const& borrow);

        /// 0 when @a index is not a guid box.
        uint64 RawGuidAt(lua_State* L, int index);
        uint64 OwnerGuidAt(lua_State* L, int index);
        bool RawHandleAt(lua_State* L, int index, Handle& out);
        bool RawBorrowAt(lua_State* L, int index, Borrow& out);

        // ---- the argument types a method may ask for ----------------------
        //
        // Declared, never defined here, so a method file that asks for a type
        // nobody implemented fails to LINK rather than instantiating the
        // primary template and failing somewhere less obvious. The fixed-width
        // set is deliberate: int, long long and size_t are spelt differently
        // on the three toolchains this builds with, and a method that asks for
        // one of those is asking a question with a different answer per
        // platform.

        template <> bool Api::Check<bool>(int narg);
        template <> float Api::Check<float>(int narg);
        template <> double Api::Check<double>(int narg);
        template <> int8 Api::Check<int8>(int narg);
        template <> int16 Api::Check<int16>(int narg);
        template <> int32 Api::Check<int32>(int narg);
        template <> int64 Api::Check<int64>(int narg);
        template <> uint8 Api::Check<uint8>(int narg);
        template <> uint16 Api::Check<uint16>(int narg);
        template <> uint32 Api::Check<uint32>(int narg);
        template <> uint64 Api::Check<uint64>(int narg);
        template <> char const* Api::Check<char const*>(int narg);
        template <> std::string Api::Check<std::string>(int narg);
        template <> ObjectGuid Api::Check<ObjectGuid>(int narg);

        template <> Object* Api::CheckObj<Object>(int narg, bool error);
        template <> WorldObject* Api::CheckObj<WorldObject>(int narg, bool error);
        template <> Unit* Api::CheckObj<Unit>(int narg, bool error);
        template <> Player* Api::CheckObj<Player>(int narg, bool error);
        template <> Creature* Api::CheckObj<Creature>(int narg, bool error);
        template <> GameObject* Api::CheckObj<GameObject>(int narg, bool error);
        template <> Item* Api::CheckObj<Item>(int narg, bool error);
        template <> Corpse* Api::CheckObj<Corpse>(int narg, bool error);
        template <> Map* Api::CheckObj<Map>(int narg, bool error);
        template <> Group* Api::CheckObj<Group>(int narg, bool error);
        template <> Guild* Api::CheckObj<Guild>(int narg, bool error);
        // Const, and not by oversight: a Quest is shared template data that
        // every player on the server reads. Handing a script a mutable
        // pointer to it would let one script's typo change the quest for
        // everyone until the next restart.
        template <> Quest const* Api::CheckObj<Quest const>(int narg, bool error);
        template <> Spell* Api::CheckObj<Spell>(int narg, bool error);
        template <> Aura* Api::CheckObj<Aura>(int narg, bool error);
        template <> WorldPacket* Api::CheckObj<WorldPacket>(int narg, bool error);
        template <> BattleGround* Api::CheckObj<BattleGround>(int narg, bool error);
    }
}

#endif //MANGOS_LUAU_API_H
