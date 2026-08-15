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

// Object -- what an Item and a WorldObject have in common: an entry, a guid,
// and the block of update fields the client is told about.
//
// The GetValue/SetValue family is deliberately kept. It is the ugliest part of
// the surface and the one a script author reaches for when the core has no
// method for what they need, and taking it away would not make the need go
// away -- it would make it unmeetable.

#include "LuaApi.h"
#include "Methods.h"

#include "Object.h"
#include "ObjectGuid.h"

#include "lua.h"
#include "lualib.h"

namespace scripting
{
    namespace api
    {
        namespace
        {
            /**
             * A field index this object actually has.
             *
             * EVERY accessor below asserts on an index past the end of the
             * block -- MANGOS_ASSERT, which is live in a release build and
             * takes the process with it -- so an index taken unchecked from a
             * script was a way to stop the server from Lua, in one line, with
             * no privilege beyond writing a script.
             *
             * The bound has to be the object's own and cannot be a constant:
             * a Creature's block ends at UNIT_END and a Player's runs a long
             * way past it, which is also why reading a PLAYER_ field off a
             * creature is the same bug wearing a method name.
             *
             * @a span is how many consecutive uint32 the accessor touches --
             * two for the 64-bit guid fields, which is the difference between
             * `index < count` and `index + 1 < count`.
             */
            uint16 FieldAt(Api& a, Object const* obj, int narg, uint32 span = 1)
            {
                uint32 const index = a.Check<uint32>(narg);

                // Widened, because the sum is the thing being bounded: an
                // index of 0xFFFFFFFF plus a span of two wraps to 1 in 32 bits
                // and sails past a check written at that width.
                if (uint64(index) + span > uint64(obj->GetValuesCount()))
                {
                    luaL_argerror(a.L, narg, "field index is past the end of "
                                             "this object's block");
                }
                return uint16(index);
            }

            /// The byte or half-word offset inside one field. Asserted too,
            /// and for the same reason.
            uint8 OffsetAt(Api& a, int narg, uint8 count)
            {
                uint8 const offset = a.Check<uint8>(narg);
                if (offset >= count)
                {
                    luaL_argerror(a.L, narg, "offset does not fit inside a "
                                             "field");
                }
                return offset;
            }

            /// Returns true if the flag at @a index is set.
            int HasFlag(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint32 const flag = a.Check<uint32>(3);
                a.Push(obj->HasFlag(index, flag));
                return 1;
            }

            /// Returns true once the object has been added to its map.
            int IsInWorld(Api& a, Object* obj)
            {
                a.Push(obj->IsInWorld());
                return 1;
            }

            int GetInt32Value(Api& a, Object* obj)
            {
                a.Push(obj->GetInt32Value(FieldAt(a, obj, 2)));
                return 1;
            }

            int GetUInt32Value(Api& a, Object* obj)
            {
                a.Push(obj->GetUInt32Value(FieldAt(a, obj, 2)));
                return 1;
            }

            int GetFloatValue(Api& a, Object* obj)
            {
                a.Push(obj->GetFloatValue(FieldAt(a, obj, 2)));
                return 1;
            }

            int GetByteValue(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint8 const offset = OffsetAt(a, 3, 4);
                a.Push(obj->GetByteValue(index, offset));
                return 1;
            }

            int GetUInt16Value(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint8 const offset = OffsetAt(a, 3, 2);
                a.Push(obj->GetUInt16Value(index, offset));
                return 1;
            }

            /**
             * The guid stored in the 64-bit field at @a index.
             *
             * This replaces Eluna's GetUInt64Value, which is registered here
             * as unimplemented. A Lua number is a double and holds 53 bits;
             * every 64-bit update field in the game is a guid, so handing one
             * back as a number would round exactly the values a script most
             * wants to compare. Returning the guid box loses nothing and
             * cannot be silently wrong.
             */
            int GetGuidValue(Api& a, Object* obj)
            {
                // Two fields wide: GetUInt64Value underneath asserts on
                // index + 1, not on index.
                a.Push(obj->GetGuidValue(FieldAt(a, obj, 2, 2)));
                return 1;
            }

            int GetScale(Api& a, Object* obj)
            {
                a.Push(obj->GetObjectScale());
                return 1;
            }

            int GetEntry(Api& a, Object* obj)
            {
                a.Push(obj->GetEntry());
                return 1;
            }

            int GetGUID(Api& a, Object* obj)
            {
                a.Push(obj->GetObjectGuid());
                return 1;
            }

            /**
             * The low part of the guid.
             *
             * Unique only within a map in this core, which is not a detail: a
             * creature in an instance keeps the low guid its database spawn
             * was given, so two instances of the same dungeon hand back the
             * same number. Identify a creature by the pair (map, low guid),
             * or just keep the guid itself.
             */
            int GetGUIDLow(Api& a, Object* obj)
            {
                a.Push(obj->GetGUIDLow());
                return 1;
            }

            int GetTypeId(Api& a, Object* obj)
            {
                a.Push(uint8(obj->GetTypeId()));
                return 1;
            }

            int SetFlag(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint32 const flag = a.Check<uint32>(3);
                obj->SetFlag(index, flag);
                return 0;
            }

            int RemoveFlag(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint32 const flag = a.Check<uint32>(3);
                obj->RemoveFlag(index, flag);
                return 0;
            }

            int SetInt32Value(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                obj->SetInt32Value(index, a.Check<int32>(3));
                return 0;
            }

            int SetUInt32Value(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                obj->SetUInt32Value(index, a.Check<uint32>(3));
                return 0;
            }

            int UpdateUInt32Value(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                obj->UpdateUInt32Value(index, a.Check<uint32>(3));
                return 0;
            }

            int SetFloatValue(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                obj->SetFloatValue(index, a.Check<float>(3));
                return 0;
            }

            int SetByteValue(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint8 const offset = OffsetAt(a, 3, 4);
                obj->SetByteValue(index, offset, a.Check<uint8>(4));
                return 0;
            }

            int SetUInt16Value(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint8 const offset = OffsetAt(a, 3, 2);
                obj->SetUInt16Value(index, offset, a.Check<uint16>(4));
                return 0;
            }

            int SetInt16Value(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2);
                uint8 const offset = OffsetAt(a, 3, 2);
                obj->SetInt16Value(index, offset, a.Check<int16>(4));
                return 0;
            }

            /// See GetGuidValue for why there is no SetUInt64Value.
            int SetGuidValue(Api& a, Object* obj)
            {
                uint16 const index = FieldAt(a, obj, 2, 2);
                obj->SetGuidValue(index, a.Check<ObjectGuid>(3));
                return 0;
            }

            int SetScale(Api& a, Object* obj)
            {
                obj->SetObjectScale(a.Check<float>(2));
                return 0;
            }
        }

        MethodEntry const* ObjectMethods(std::size_t& count)
        {
#define M(name)                                                               \
            { #name, [](Api& a, void* self) -> int                            \
                { return name(a, static_cast<Object*>(self)); } }

            static MethodEntry const table[] =
            {
                M(HasFlag),
                M(IsInWorld),
                M(GetInt32Value),
                M(GetUInt32Value),
                M(GetFloatValue),
                M(GetByteValue),
                M(GetUInt16Value),
                M(GetGuidValue),
                M(GetScale),
                M(GetEntry),
                M(GetGUID),
                M(GetGUIDLow),
                M(GetTypeId),
                M(SetFlag),
                M(RemoveFlag),
                M(SetInt32Value),
                M(SetUInt32Value),
                M(UpdateUInt32Value),
                M(SetFloatValue),
                M(SetByteValue),
                M(SetUInt16Value),
                M(SetInt16Value),
                M(SetGuidValue),
                M(SetScale),

                // Names Eluna had that this core answers differently, kept so
                // a script ported from it is told what to use instead of
                // failing with "attempt to call a nil value".
                { "GetUInt64Value", nullptr },
                { "SetUInt64Value", nullptr },

                // The To* family is gone and is not coming back. A guid knows
                // what it names, so a player guid already answers Unit's
                // methods and a creature guid already answers Creature's;
                // there is nothing to convert. Use GetTypeId to ask what
                // something is.
                { "ToPlayer", nullptr },
                { "ToCreature", nullptr },
                { "ToUnit", nullptr },
                { "ToGameObject", nullptr },
                { "ToCorpse", nullptr },
            };

#undef M

            count = sizeof(table) / sizeof(table[0]);
            return table;
        }
    }
}
