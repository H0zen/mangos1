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

#include "LuaApi.h"
#include "Methods.h"

#include "BattleGround.h"
#include "BattleGroundMgr.h"
#include "Corpse.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptHost.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "WorldPacket.h"

#include "lua.h"
#include "lualib.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace scripting
{
    namespace api
    {
        char const MT_GUID[]   = "mangos.guid";
        char const MT_HANDLE[] = "mangos.handle";
        char const MT_BORROW[] = "mangos.borrow";

        namespace
        {
            /// Registry keys. Strings rather than refs because they are read
            /// once per call at most and read clearly in a stack dump.
            char const REG_MAP[]     = "mangos.map";
            char const REG_CLASSES[] = "mangos.classes";
            char const REG_LOADING[] = "mangos.loading";

            /**
             * A guid, and the owner it needs to be findable again.
             *
             * The owner is 0 for everything except an item, and an item is
             * the one thing in the game that is NOT findable from its guid:
             * there is no global item registry in this core, only
             * Player::GetItemByGuid. Carrying the owner is what makes an item
             * a first-class receiver instead of a value that only some call
             * sites can turn back into an object.
             *
             * The owner is filled in whenever it can be. An Item pushed by a
             * method knows its own owner and is always complete; one arriving
             * in an event payload gets whichever player that payload carries,
             * which is every item-bearing event but two -- item.on_dummy_effect
             * and core.on_effect_dummy, whose target may be an item and which
             * name no player. An item from those two resolves to nothing, and
             * that is the honest answer rather than a wrong one.
             */
            struct GuidBox
            {
                uint64 raw;
                uint64 owner;
            };

            struct HandleBox { Handle value; };
            struct BorrowBox { Borrow value; };

            /// Which flattened method table answers for this guid.
            TypeClass ClassOfGuid(ObjectGuid guid)
            {
                switch (guid.GetHigh())
                {
                    case HIGHGUID_PLAYER:        return TypeClass::Player;
                    case HIGHGUID_UNIT:
                    case HIGHGUID_PET:           return TypeClass::Creature;
                    case HIGHGUID_GAMEOBJECT:
                    case HIGHGUID_TRANSPORT:
                    case HIGHGUID_MO_TRANSPORT:  return TypeClass::GameObject;
                    case HIGHGUID_ITEM:          return TypeClass::Item;
                    case HIGHGUID_CORPSE:        return TypeClass::Corpse;

                    // A DynamicObject has no methods of its own worth having,
                    // but it is a WorldObject and answering with the base is
                    // more useful than answering with nothing.
                    case HIGHGUID_DYNAMICOBJECT: return TypeClass::WorldObject;
                    default:                     return TypeClass::None;
                }
            }

            TypeClass ClassOfDomain(Domain domain)
            {
                switch (domain)
                {
                    case Domain::Map:          return TypeClass::Map;
                    case Domain::Group:        return TypeClass::Group;
                    case Domain::Guild:        return TypeClass::Guild;
                    case Domain::Quest:        return TypeClass::Quest;
                    case Domain::BattleGround: return TypeClass::BattleGround;
                    case Domain::Spell:        return TypeClass::Spell;
                    case Domain::Aura:         return TypeClass::Aura;
                    case Domain::Packet:       return TypeClass::Packet;
                    default:                   return TypeClass::None;
                }
            }

            GuidBox* GuidBoxAt(lua_State* L, int index)
            {
                if (!lua_isuserdata(L, index))
                {
                    return nullptr;
                }
                if (!lua_getmetatable(L, index))
                {
                    return nullptr;
                }
                luaL_getmetatable(L, MT_GUID);
                bool const match = lua_rawequal(L, -1, -2) != 0;
                lua_pop(L, 2);
                return match ? static_cast<GuidBox*>(lua_touserdata(L, index))
                             : nullptr;
            }

            template <typename Box>
            Box* BoxAt(lua_State* L, int index, char const* mt)
            {
                if (!lua_isuserdata(L, index))
                {
                    return nullptr;
                }
                if (!lua_getmetatable(L, index))
                {
                    return nullptr;
                }
                luaL_getmetatable(L, mt);
                bool const match = lua_rawequal(L, -1, -2) != 0;
                lua_pop(L, 2);
                return match ? static_cast<Box*>(lua_touserdata(L, index))
                             : nullptr;
            }
        }

        // ---- the boxes -----------------------------------------------------

        void PushRawGuid(lua_State* L, uint64 raw)
        {
            PushOwnedGuid(L, raw, 0);
        }

        void PushOwnedGuid(lua_State* L, uint64 raw, uint64 owner)
        {
            if (!raw)
            {
                lua_pushnil(L);
                return;
            }

            GuidBox* box = static_cast<GuidBox*>(
                lua_newuserdata(L, sizeof(GuidBox)));
            box->raw = raw;
            box->owner = owner;
            luaL_getmetatable(L, MT_GUID);
            lua_setmetatable(L, -2);
        }

        void PushRawHandle(lua_State* L, Handle const& handle)
        {
            if (handle.IsEmpty())
            {
                lua_pushnil(L);
                return;
            }

            HandleBox* box = static_cast<HandleBox*>(
                lua_newuserdata(L, sizeof(HandleBox)));
            box->value = handle;
            luaL_getmetatable(L, MT_HANDLE);
            lua_setmetatable(L, -2);
        }

        void PushRawBorrow(lua_State* L, Borrow const& borrow)
        {
            if (borrow.IsEmpty())
            {
                lua_pushnil(L);
                return;
            }

            BorrowBox* box = static_cast<BorrowBox*>(
                lua_newuserdata(L, sizeof(BorrowBox)));
            box->value = borrow;
            luaL_getmetatable(L, MT_BORROW);
            lua_setmetatable(L, -2);
        }

        uint64 RawGuidAt(lua_State* L, int index)
        {
            GuidBox const* box = GuidBoxAt(L, index);
            return box ? box->raw : 0;
        }

        uint64 OwnerGuidAt(lua_State* L, int index)
        {
            GuidBox const* box = GuidBoxAt(L, index);
            return box ? box->owner : 0;
        }

        bool RawHandleAt(lua_State* L, int index, Handle& out)
        {
            HandleBox const* box = BoxAt<HandleBox>(L, index, MT_HANDLE);
            if (!box)
            {
                return false;
            }
            out = box->value;
            return true;
        }

        bool RawBorrowAt(lua_State* L, int index, Borrow& out)
        {
            BorrowBox const* box = BoxAt<BorrowBox>(L, index, MT_BORROW);
            if (!box)
            {
                return false;
            }
            out = box->value;
            return true;
        }

        Map* BoundMapOf(lua_State* L)
        {
            lua_getfield(L, LUA_REGISTRYINDEX, REG_MAP);
            Map* map = static_cast<Map*>(lua_tolightuserdata(L, -1));
            lua_pop(L, 1);
            return map;
        }

        void SetLoading(lua_State* L, bool loading)
        {
            lua_pushboolean(L, loading ? 1 : 0);
            lua_setfield(L, LUA_REGISTRYINDEX, REG_LOADING);
        }

        bool IsLoading(lua_State* L)
        {
            lua_getfield(L, LUA_REGISTRYINDEX, REG_LOADING);
            bool const loading = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return loading;
        }

        // ---- Api -----------------------------------------------------------

        int32 Api::BoundMapId() const
        {
            return m_map ? int32(m_map->GetId()) : -1;
        }

        bool Api::IsNoneOrNil(int narg) const
        {
            return lua_isnoneornil(L, narg);
        }

        void Api::Push()                    { lua_pushnil(L); }
        void Api::Push(bool value)          { lua_pushboolean(L, value ? 1 : 0); }
        void Api::Push(float value)         { lua_pushnumber(L, double(value)); }
        void Api::Push(double value)        { lua_pushnumber(L, value); }
        void Api::PushSigned(int64 value)   { lua_pushnumber(L, double(value)); }
        void Api::PushUnsigned(uint64 value){ lua_pushnumber(L, double(value)); }

        void Api::Push(char const* value)
        {
            if (value)
            {
                lua_pushstring(L, value);
            }
            else
            {
                lua_pushnil(L);
            }
        }

        void Api::Push(std::string const& value)
        {
            lua_pushlstring(L, value.c_str(), value.size());
        }

        void Api::Push(ObjectGuid guid)
        {
            PushRawGuid(L, guid.GetRawValue());
        }

        void Api::Push(Object const* obj)
        {
            if (!obj)
            {
                lua_pushnil(L);
                return;
            }

            // An Item knows its owner, so the box it makes is complete
            // whatever route it arrived by. This is the branch that keeps the
            // manifest's item/player pairing from being the only way an item
            // can ever become a receiver. There is no Object::ToItem in this
            // core, so the test is the type id itself.
            if (obj->GetTypeId() == TYPEID_ITEM)
            {
                Push(static_cast<Item const*>(obj));
                return;
            }

            PushRawGuid(L, obj->GetObjectGuid().GetRawValue());
        }

        void Api::Push(WorldObject const* obj) { Push(static_cast<Object const*>(obj)); }
        void Api::Push(Unit const* unit)       { Push(static_cast<Object const*>(unit)); }
        void Api::Push(Player const* player)   { Push(static_cast<Object const*>(player)); }
        void Api::Push(Creature const* c)      { Push(static_cast<Object const*>(c)); }
        void Api::Push(GameObject const* go)   { Push(static_cast<Object const*>(go)); }
        void Api::Push(Corpse const* corpse)   { Push(static_cast<Object const*>(corpse)); }

        void Api::Push(Item const* item)
        {
            if (!item)
            {
                lua_pushnil(L);
                return;
            }

            PushOwnedGuid(L, item->GetObjectGuid().GetRawValue(),
                          item->GetOwnerGuid().GetRawValue());
        }

        void Api::Push(Map const* map)
        {
            PushRawHandle(L, map ? Handle{ uint64(map->GetId()), Domain::Map }
                                 : Handle{ 0, Domain::None });
        }

        void Api::Push(Group const* group) { PushRawHandle(L, HandleOf(group)); }
        void Api::Push(Guild const* guild) { PushRawHandle(L, HandleOf(guild)); }
        void Api::Push(Quest const* quest) { PushRawHandle(L, HandleOf(quest)); }
        void Api::Push(BattleGround const* bg) { PushRawHandle(L, HandleOf(bg)); }

        // A borrow is stamped with the epoch of the call that is running now,
        // so one a method hands back dies with that call exactly as one the
        // world lent does. There is no way to mint a longer-lived borrow here
        // and there should not be.
        void Api::Push(Spell const* spell)
        {
            PushRawBorrow(L, spell ? Lend(Domain::Spell, spell)
                                   : Borrow{ nullptr, 0, Domain::None });
        }

        void Api::Push(Aura const* aura)
        {
            PushRawBorrow(L, aura ? Lend(Domain::Aura, aura)
                                  : Borrow{ nullptr, 0, Domain::None });
        }

        void Api::Push(WorldPacket const* packet)
        {
            PushRawBorrow(L, packet ? Lend(Domain::Packet, packet)
                                    : Borrow{ nullptr, 0, Domain::None });
        }

        WorldObject* Api::Resolve(ObjectGuid guid) const
        {
            if (guid.IsEmpty())
            {
                return nullptr;
            }

            if (m_map)
            {
                if (WorldObject* found = m_map->GetWorldObject(guid))
                {
                    return found;
                }
            }

            // A player is the one thing findable without a map, which is why
            // a login handler running in the world state can still ask one
            // its name. Everything else genuinely needs the map it lives in.
            if (guid.IsPlayer())
            {
                return sObjectMgr.GetPlayer(guid);
            }

            return nullptr;
        }

        // ---- arguments in --------------------------------------------------

        template <> bool Api::Check<bool>(int narg)
        {
            luaL_checkany(L, narg);
            return lua_toboolean(L, narg) != 0;
        }

        namespace
        {
            /**
             * A real number, and actually a number.
             *
             * NaN and the infinities are perfectly ordinary Lua values -- 0/0
             * produces one without a word of complaint -- and every one of
             * them is a coordinate, a radius or a facing as far as this layer
             * is concerned. A NaN reaching Placement or the grid is the
             * corruption that never gets diagnosed, because every comparison
             * made with it afterwards is false and nothing looks wrong.
             *
             * The integral path already refuses what it cannot represent; this
             * is the same promise kept for the type that carries most of the
             * spatial arguments in this API.
             */
            double CheckFinite(lua_State* L, int narg)
            {
                double const value = luaL_checknumber(L, narg);
                if (!std::isfinite(value))
                {
                    luaL_argerror(L, narg, "a finite number is expected here");
                }
                return value;
            }

            /**
             * A number, refused rather than folded when it does not fit.
             *
             * Lua has one numeric type and it is a double, so a script can
             * hand a method 4294967296 where a uint32 was wanted, or -1 where
             * an unsigned was. Casting quietly is how a script ends up
             * setting a health of 0 and nobody can say why, so the range is
             * checked and a script that is out of it is told so.
             *
             * The upper bound is `>=` and not `>`, which matters only at the
             * two widest types and matters absolutely there: 2^63 and 2^64 are
             * not representable as int64 and uint64, but they ARE exactly
             * representable as doubles, and `double(max)` rounds up to them.
             * Written with `>`, the one value that cannot be converted was the
             * one value that passed, and the conversion is undefined.
             */
            template <typename T>
            T CheckIntegral(lua_State* L, int narg)
            {
                double const value = CheckFinite(L, narg);
                if (value < double(std::numeric_limits<T>::min()) ||
                    value >= double(std::numeric_limits<T>::max()) + 1.0)
                {
                    luaL_error(L, "argument %d is %f, which does not fit the "
                                  "range this method accepts", narg, value);
                }
                return T(value);
            }
        }

        template <> float Api::Check<float>(int narg)
        {
            return float(CheckFinite(L, narg));
        }

        template <> double Api::Check<double>(int narg)
        {
            return CheckFinite(L, narg);
        }

        template <> int8 Api::Check<int8>(int narg)     { return CheckIntegral<int8>(L, narg); }
        template <> int16 Api::Check<int16>(int narg)   { return CheckIntegral<int16>(L, narg); }
        template <> int32 Api::Check<int32>(int narg)   { return CheckIntegral<int32>(L, narg); }
        template <> int64 Api::Check<int64>(int narg)   { return CheckIntegral<int64>(L, narg); }
        template <> uint8 Api::Check<uint8>(int narg)   { return CheckIntegral<uint8>(L, narg); }
        template <> uint16 Api::Check<uint16>(int narg) { return CheckIntegral<uint16>(L, narg); }
        template <> uint32 Api::Check<uint32>(int narg) { return CheckIntegral<uint32>(L, narg); }
        template <> uint64 Api::Check<uint64>(int narg) { return CheckIntegral<uint64>(L, narg); }

        template <> char const* Api::Check<char const*>(int narg)
        {
            return luaL_checkstring(L, narg);
        }

        template <> std::string Api::Check<std::string>(int narg)
        {
            std::size_t length = 0;
            char const* text = luaL_checklstring(L, narg, &length);
            return std::string(text, length);
        }

        template <> ObjectGuid Api::Check<ObjectGuid>(int narg)
        {
            uint64 const raw = RawGuidAt(L, narg);
            if (!raw)
            {
                luaL_argerror(L, narg, "expected an object");
            }
            return ObjectGuid(raw);
        }

        // ---- receivers and arguments that are objects ----------------------

        namespace
        {
            /**
             * The object at @a narg, whatever kind of box it came in.
             *
             * Returns null for a guid that names something no longer there,
             * which is a legitimate answer: a script may ask about a creature
             * that has since despawned, and being told "nothing" is more
             * useful than a script error.
             */
            void* ResolveAt(Api& api, int narg, TypeClass want)
            {
                lua_State* L = api.L;

                if (GuidBox const* box = GuidBoxAt(L, narg))
                {
                    ObjectGuid const guid(box->raw);

                    // The one type with no global registry. Its owner rides
                    // along in the box precisely so this line can exist.
                    //
                    // Keyed on what the guid IS, not on what the caller asked
                    // for, and that is the whole of it: ItemMethods is empty,
                    // so every method an item answers is inherited from Object
                    // and arrives here with want == Object. Testing `want`
                    // sent all of them down the branch below, where
                    // Map::GetWorldObject has no case for HIGHGUID_ITEM and
                    // answers null -- which made every single method call on
                    // an item fail with "no longer there", and made the owner
                    // this box carries do nothing at all.
                    if (guid.IsItem())
                    {
                        if (want != TypeClass::Item &&
                            want != TypeClass::Object)
                        {
                            return nullptr;
                        }

                        WorldObject* holder =
                            api.Resolve(ObjectGuid(box->owner));
                        Player* owner = holder ? holder->ToPlayer() : nullptr;
                        Item* item = owner ? owner->GetItemByGuid(guid)
                                           : nullptr;

                        // Object is a base of Item, and the upcast is spelt
                        // out for the same reason it is spelt out below.
                        return want == TypeClass::Object
                                   ? static_cast<void*>(
                                         static_cast<Object*>(item))
                                   : static_cast<void*>(item);
                    }

                    WorldObject* obj = api.Resolve(guid);
                    if (!obj)
                    {
                        return nullptr;
                    }

                    // The cast to Object is spelt out rather than left to the
                    // implicit conversion to void*: a base subobject is only
                    // guaranteed to share its address with the derived object
                    // under the single, non-virtual inheritance this
                    // hierarchy happens to use, and a void* built from the
                    // wrong one of those would be undetectable.
                    switch (want)
                    {
                        case TypeClass::Object:      return static_cast<Object*>(obj);
                        case TypeClass::WorldObject: return obj;
                        case TypeClass::Unit:        return obj->ToUnit();
                        case TypeClass::Player:      return obj->ToPlayer();
                        case TypeClass::Creature:    return obj->ToCreature();
                        case TypeClass::GameObject:  return obj->ToGameObject();
                        case TypeClass::Corpse:      return obj->ToCorpse();
                        default:                     return nullptr;
                    }
                }

                Handle handle{ 0, Domain::None };
                if (RawHandleAt(L, narg, handle))
                {
                    if (ClassOfDomain(handle.domain) != want)
                    {
                        return nullptr;
                    }

                    uint32 const id = uint32(handle.id);
                    switch (want)
                    {
                        case TypeClass::Map:
                            // A handle carries the map id and not the
                            // instance, so an instanced map is only findable
                            // while it is the one the call happened on. That
                            // covers every event -- a script is asking about
                            // the map it is standing in -- and nothing else
                            // can be answered honestly.
                            if (Map* bound = api.BoundMap())
                            {
                                if (bound->GetId() == id)
                                {
                                    return bound;
                                }
                            }
                            return sMapMgr.FindMap(id);

                        case TypeClass::Group:
                            return sObjectMgr.GetGroupById(id);
                        case TypeClass::Guild:
                            return sGuildMgr.GetGuildById(id);
                        case TypeClass::Quest:
                            // const_cast is safe here and only here: the
                            // methods bound to TypeClass::Quest are all
                            // getters, and CheckObj hands quests out const.
                            return const_cast<Quest*>(
                                sObjectMgr.GetQuestTemplate(id));
                        case TypeClass::BattleGround:
                            return sBattleGroundMgr.GetBattleGround(
                                id, BATTLEGROUND_TYPE_NONE);
                        default:
                            return nullptr;
                    }
                }

                Borrow borrow{ nullptr, 0, Domain::None };
                if (RawBorrowAt(L, narg, borrow))
                {
                    if (ClassOfDomain(borrow.domain) != want)
                    {
                        return nullptr;
                    }

                    // The epoch is the whole point of a borrow: the pointer
                    // is still a pointer, but using it after the call that
                    // lent it has returned is DETECTED rather than silently
                    // reading freed memory.
                    if (!detail::IsBorrowLive(borrow))
                    {
                        luaL_error(L, "this value was only valid during the "
                                      "call that handed it over, and that "
                                      "call has returned");
                    }
                    return borrow.target;
                }

                return nullptr;
            }

            template <typename T>
            T* CheckAs(Api& api, int narg, TypeClass want, bool error,
                       char const* what)
            {
                if (api.IsNoneOrNil(narg))
                {
                    if (error)
                    {
                        luaL_argerror(api.L, narg, what);
                    }
                    return nullptr;
                }

                void* found = ResolveAt(api, narg, want);
                if (!found && error)
                {
                    luaL_argerror(api.L, narg, what);
                }
                return static_cast<T*>(found);
            }
        }

#define MANGOS_LUA_CHECKOBJ(type, cls)                                        \
        template <> type* Api::CheckObj<type>(int narg, bool error)           \
        {                                                                     \
            return CheckAs<type>(*this, narg, TypeClass::cls, error, #type);  \
        }

        MANGOS_LUA_CHECKOBJ(Object, Object)
        MANGOS_LUA_CHECKOBJ(WorldObject, WorldObject)
        MANGOS_LUA_CHECKOBJ(Unit, Unit)
        MANGOS_LUA_CHECKOBJ(Player, Player)
        MANGOS_LUA_CHECKOBJ(Creature, Creature)
        MANGOS_LUA_CHECKOBJ(GameObject, GameObject)
        MANGOS_LUA_CHECKOBJ(Item, Item)
        MANGOS_LUA_CHECKOBJ(Corpse, Corpse)
        MANGOS_LUA_CHECKOBJ(Map, Map)
        MANGOS_LUA_CHECKOBJ(Group, Group)
        MANGOS_LUA_CHECKOBJ(Guild, Guild)
        MANGOS_LUA_CHECKOBJ(Spell, Spell)
        MANGOS_LUA_CHECKOBJ(Aura, Aura)
        MANGOS_LUA_CHECKOBJ(WorldPacket, Packet)
        MANGOS_LUA_CHECKOBJ(BattleGround, BattleGround)

#undef MANGOS_LUA_CHECKOBJ

        template <> Quest const* Api::CheckObj<Quest const>(int narg, bool error)
        {
            return CheckAs<Quest const>(*this, narg, TypeClass::Quest, error,
                                        "Quest");
        }

        // ---- calling a method ----------------------------------------------

        namespace
        {
            /**
             * The one C function every method is reached through.
             *
             * Upvalue 1 is the entry, upvalue 2 the type its receiver must
             * resolve to. Both are fixed at registration, so the per-call work
             * is one pointer read, one resolution and the call itself.
             */
            int Thunk(lua_State* L)
            {
                MethodEntry const* entry = static_cast<MethodEntry const*>(
                    lua_tolightuserdata(L, lua_upvalueindex(1)));
                TypeClass const want =
                    TypeClass(lua_tointeger(L, lua_upvalueindex(2)));

                Api api(L, BoundMapOf(L));

                void* self = ResolveAt(api, 1, want);
                if (!self)
                {
                    luaL_error(L, "%s was called on something that is no "
                                  "longer there", entry->name);
                }

                return entry->fn(api, self);
            }

            /// A name the core knows but does not answer to. Registering it
            /// is what turns "attempt to call a nil value" four frames from
            /// the cause into a sentence naming the method.
            int NotImplemented(lua_State* L)
            {
                luaL_error(L, "%s is not implemented in this core",
                           lua_tostring(L, lua_upvalueindex(1)));
                return 0;
            }

            /// Shared by the three metatables: find the receiver's class,
            /// then the method in that class's flattened table.
            ///
            /// A box whose domain has no class at all is told so by name. Most
            /// of the manifest's domains have none yet -- a spellinfo, a
            /// channel, a proc, a cast-target list all cross the seam as boxes
            /// this layer cannot give methods to -- and answering nil made
            /// every one of them fail four frames later as "attempt to call a
            /// nil value", which names neither the value nor the reason. It is
            /// the same argument the NotImplemented closure below is built on.
            int LookupIn(lua_State* L, TypeClass cls)
            {
                if (cls == TypeClass::None)
                {
                    luaL_error(L, "this value carries no methods in this core; "
                                  "it can be passed on and compared, not "
                                  "called into");
                }

                lua_getfield(L, LUA_REGISTRYINDEX, REG_CLASSES);
                lua_rawgeti(L, -1, int(cls));
                lua_pushvalue(L, 2);
                lua_rawget(L, -2);
                return 1;
            }

            int Lua_BorrowAlive(lua_State* L);

            int Index_Guid(lua_State* L)
            {
                GuidBox const* box = GuidBoxAt(L, 1);
                return LookupIn(L, box ? ClassOfGuid(ObjectGuid(box->raw))
                                       : TypeClass::None);
            }

            int Index_Handle(lua_State* L)
            {
                Handle handle{ 0, Domain::None };
                RawHandleAt(L, 1, handle);
                return LookupIn(L, ClassOfDomain(handle.domain));
            }

            int Index_Borrow(lua_State* L)
            {
                Borrow borrow{ nullptr, 0, Domain::None };
                RawBorrowAt(L, 1, borrow);

                // IsLive cannot be an ordinary method, and the reason is the
                // whole point of it: every ordinary method goes through the
                // thunk, which raises a script error when the receiver is
                // dead. The one question a dead borrow must still be able to
                // answer therefore has to bypass that.
                char const* key = lua_tostring(L, 2);
                if (key && std::strcmp(key, "IsLive") == 0)
                {
                    lua_pushcfunction(L, Lua_BorrowAlive, "IsLive");
                    return 1;
                }

                return LookupIn(L, ClassOfDomain(borrow.domain));
            }

            int Lua_GuidToString(lua_State* L)
            {
                GuidBox const* box = GuidBoxAt(L, 1);
                if (!box)
                {
                    lua_pushstring(L, "none");
                    return 1;
                }

                std::string const text = ObjectGuid(box->raw).GetString();
                lua_pushlstring(L, text.c_str(), text.size());
                return 1;
            }

            int Lua_GuidEquals(lua_State* L)
            {
                lua_pushboolean(L, RawGuidAt(L, 1) == RawGuidAt(L, 2) ? 1 : 0);
                return 1;
            }

            /// A borrow answers one question honestly: am I still usable?
            int Lua_BorrowAlive(lua_State* L)
            {
                Borrow borrow{ nullptr, 0, Domain::None };
                bool const live = RawBorrowAt(L, 1, borrow) &&
                                  detail::IsBorrowLive(borrow);
                lua_pushboolean(L, live ? 1 : 0);
                return 1;
            }

            void MakeMetatable(lua_State* L, char const* name,
                               lua_CFunction index, lua_CFunction tostring,
                               lua_CFunction eq)
            {
                luaL_newmetatable(L, name);

                lua_pushcfunction(L, index, "__index");
                lua_setfield(L, -2, "__index");

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

                // A script may hold one of these and compare it. It may not
                // reach the metatable and rewrite what the type can do.
                lua_pushstring(L, name);
                lua_setfield(L, -2, "__metatable");

                lua_pop(L, 1);
            }

            /**
             * Build one class's method table, flattened over its parent.
             *
             * Flattening at load rather than chaining __index at call time is
             * the reason a Player method costs one rawget: Player's table
             * already holds Object's, WorldObject's and Unit's entries.
             */
            void DefineClass(lua_State* L, TypeClass cls, TypeClass parent,
                             MethodEntry const* methods, std::size_t count)
            {
                lua_getfield(L, LUA_REGISTRYINDEX, REG_CLASSES);
                int const classes = lua_gettop(L);

                lua_newtable(L);
                int const table = lua_gettop(L);

                if (parent != TypeClass::None)
                {
                    lua_rawgeti(L, classes, int(parent));
                    int const from = lua_gettop(L);

                    lua_pushnil(L);
                    while (lua_next(L, from))
                    {
                        // key at -2, value at -1; rawset eats a copy of both
                        lua_pushvalue(L, -2);
                        lua_pushvalue(L, -2);
                        lua_rawset(L, table);
                        lua_pop(L, 1);
                    }

                    lua_pop(L, 1);
                }

                for (std::size_t i = 0; i < count; ++i)
                {
                    MethodEntry const& entry = methods[i];

                    if (!entry.fn)
                    {
                        lua_pushstring(L, entry.name);
                        lua_pushcclosure(L, NotImplemented, entry.name, 1);
                    }
                    else
                    {
                        lua_pushlightuserdata(
                            L, const_cast<MethodEntry*>(&entry));
                        lua_pushinteger(L, int(cls));
                        lua_pushcclosure(L, Thunk, entry.name, 2);
                    }

                    lua_setfield(L, table, entry.name);
                }

                // Frozen, so one script cannot rewrite what a creature can do
                // for every other script sharing the state.
                lua_setreadonly(L, table, 1);
                lua_rawseti(L, classes, int(cls));
                lua_pop(L, 1);
            }

            void DefineAll(lua_State* L)
            {
                std::size_t count = 0;
                MethodEntry const* table = nullptr;

#define MANGOS_LUA_CLASS(cls, parent, source)                                 \
                table = source(count);                                        \
                DefineClass(L, TypeClass::cls, TypeClass::parent, table, count)

                MANGOS_LUA_CLASS(Object, None, ObjectMethods);
                MANGOS_LUA_CLASS(WorldObject, Object, WorldObjectMethods);
                MANGOS_LUA_CLASS(Unit, WorldObject, UnitMethods);
                MANGOS_LUA_CLASS(Player, Unit, PlayerMethods);
                MANGOS_LUA_CLASS(Creature, Unit, CreatureMethods);
                MANGOS_LUA_CLASS(GameObject, WorldObject, GameObjectMethods);
                MANGOS_LUA_CLASS(Corpse, WorldObject, CorpseMethods);
                MANGOS_LUA_CLASS(Item, Object, ItemMethods);
                MANGOS_LUA_CLASS(Map, None, MapMethods);
                MANGOS_LUA_CLASS(Group, None, GroupMethods);
                MANGOS_LUA_CLASS(Guild, None, GuildMethods);
                MANGOS_LUA_CLASS(Quest, None, QuestMethods);
                MANGOS_LUA_CLASS(Spell, None, SpellMethods);
                MANGOS_LUA_CLASS(Aura, None, AuraMethods);
                MANGOS_LUA_CLASS(Packet, None, PacketMethods);
                MANGOS_LUA_CLASS(BattleGround, None, BattleGroundMethods);

#undef MANGOS_LUA_CLASS
            }
        }

        void RegisterApi(lua_State* L, Map* map)
        {
            lua_pushlightuserdata(L, map);
            lua_setfield(L, LUA_REGISTRYINDEX, REG_MAP);

            MakeMetatable(L, MT_GUID, Index_Guid, Lua_GuidToString,
                          Lua_GuidEquals);
            MakeMetatable(L, MT_HANDLE, Index_Handle, nullptr, nullptr);
            MakeMetatable(L, MT_BORROW, Index_Borrow, nullptr, nullptr);

            lua_newtable(L);
            lua_setfield(L, LUA_REGISTRYINDEX, REG_CLASSES);

            DefineAll(L);
            RegisterGlobals(L);
        }
    }
}
