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

// WorldObject -- an Object that is somewhere. This is where the port away from
// Eluna is largest, and it is not a translation but a rewrite.
//
// Eluna's spatial methods called GetPositionX, GetDistance, GetAngle and
// HasInArc on the object. None of those exist in this core and, by the comment
// standing over the declaration that replaced them, none of them ever will:
// an object HAS a placement rather than being a bag of coordinates, and every
// spatial question is asked of that component -- obj->Where().DistanceTo(...).
//
// The rewrite is not cosmetic. A Placement carries the FRAME it is in, and
// every comparison between two of them fails closed when the frames differ.
// That is what makes these methods correct for a passenger on a ship, where
// the deck is its own map and a world position for anything standing on it is
// meaningless; Eluna's versions would happily subtract two coordinates that
// were never in the same space and report a confident, wrong number.

#include "LuaApi.h"
#include "Methods.h"

#include "Creature.h"
#include "GameObject.h"
#include "GridDefines.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "Object.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "CellImpl.h"
#include "WorldPacket.h"

#include "lua.h"
#include "lualib.h"

#include <cmath>
#include <list>

namespace scripting
{
    namespace api
    {
        namespace
        {
            using Geometry::Placement;
            using Geometry::Vector3;

            /**
             * How far a search may reach.
             *
             * Every one of these methods ends in Cell::VisitAllObjects, whose
             * cost is the area it is given: the radius decides how many grid
             * cells are walked, and a script asking for a hundred thousand
             * yards walks all of them on the map's own update thread. Nothing
             * bounded that, so one line of Lua could stall a map for as long
             * as it liked without ever writing a loop.
             *
             * One grid is the ceiling because one grid is what the searchers
             * are built for -- it is already the default every caller here
             * uses -- and going past it is refused rather than quietly clamped:
             * a search that silently covers less ground than it was asked for
             * returns a wrong answer that looks like a right one.
             */
            float SearchRange(Api& a, int narg)
            {
                float const range = a.Check<float>(narg, SIZE_OF_GRIDS);
                if (range < 0.0f || range > SIZE_OF_GRIDS)
                {
                    luaL_argerror(a.L, narg, "a search range from 0 to one "
                                             "grid is expected here");
                }
                return range;
            }

            /// The point named by arguments @a narg onward, or the placement
            /// of the object given there. Every distance and angle method
            /// accepts both, exactly as Eluna's did.
            bool PlaceAt(Api& a, int narg, Placement const*& place,
                         Vector3& point, bool wants3D)
            {
                if (WorldObject* target = a.CheckObj<WorldObject>(narg, false))
                {
                    place = &target->Where();
                    return true;
                }

                place = nullptr;
                point.x = a.Check<float>(narg);
                point.y = a.Check<float>(narg + 1);
                point.z = wants3D ? a.Check<float>(narg + 2) : 0.0f;
                return true;
            }

            /// Push a list of objects as an array of guids.
            template <class T>
            void PushList(Api& a, std::list<T*> const& objects)
            {
                lua_State* L = a.L;
                lua_newtable(L);

                int index = 1;
                for (T* object : objects)
                {
                    a.Push(object);
                    lua_rawseti(L, -2, index++);
                }
            }

            /// Accepts anything within range, optionally only one entry, and
            /// never the searcher itself.
            template <class T>
            struct InRange
            {
                WorldObject const* origin;
                float range;
                uint32 entry;
                bool aliveOnly;

                bool operator()(T* candidate) const
                {
                    if (candidate == origin)
                    {
                        return false;
                    }
                    if (entry && candidate->GetEntry() != entry)
                    {
                        return false;
                    }
                    if (!origin->Where().WithinDist(candidate->Where(), range))
                    {
                        return false;
                    }
                    return true;
                }
            };

            template <>
            bool InRange<Creature>::operator()(Creature* candidate) const
            {
                if (candidate == origin)
                {
                    return false;
                }
                if (entry && candidate->GetEntry() != entry)
                {
                    return false;
                }
                if (aliveOnly && !candidate->IsAlive())
                {
                    return false;
                }
                return origin->Where().WithinDist(candidate->Where(), range);
            }

            int GetName(Api& a, WorldObject* obj)
            {
                a.Push(obj->GetName());
                return 1;
            }

            int GetMap(Api& a, WorldObject* obj)
            {
                a.Push(obj->GetMap());
                return 1;
            }

            int GetInstanceId(Api& a, WorldObject* obj)
            {
                a.Push(obj->GetInstanceId());
                return 1;
            }

            int GetMapId(Api& a, WorldObject* obj)
            {
                a.Push(obj->GetMapId());
                return 1;
            }

            int GetAreaId(Api& a, WorldObject* obj)
            {
                Placement const& where = obj->Where();
                a.Push(obj->GetTerrain()->GetAreaId(where.X(), where.Y(),
                                                    where.Z()));
                return 1;
            }

            int GetZoneId(Api& a, WorldObject* obj)
            {
                Placement const& where = obj->Where();
                a.Push(obj->GetTerrain()->GetZoneId(where.X(), where.Y(),
                                                    where.Z()));
                return 1;
            }

            int GetX(Api& a, WorldObject* obj) { a.Push(obj->Where().X()); return 1; }
            int GetY(Api& a, WorldObject* obj) { a.Push(obj->Where().Y()); return 1; }
            int GetZ(Api& a, WorldObject* obj) { a.Push(obj->Where().Z()); return 1; }

            int GetO(Api& a, WorldObject* obj)
            {
                a.Push(obj->Where().Facing());
                return 1;
            }

            /// x, y, z, o in one call.
            int GetLocation(Api& a, WorldObject* obj)
            {
                Placement const& where = obj->Where();
                a.Push(where.X());
                a.Push(where.Y());
                a.Push(where.Z());
                a.Push(where.Facing());
                return 4;
            }

            /**
             * The distance to another object or to a point, edges counted.
             *
             * "Edges counted" is what the object sizes buy: two creatures a
             * yard apart at their models are a yard apart here, not however
             * far apart their centres happen to be. Use GetExactDistance for
             * the centres.
             */
            int GetDistance(Api& a, WorldObject* obj)
            {
                Placement const* place = nullptr;
                Vector3 point;
                PlaceAt(a, 2, place, point, true);

                a.Push(place ? obj->Where().DistanceTo(*place)
                             : obj->Where().DistanceTo(point));
                return 1;
            }

            int GetDistance2d(Api& a, WorldObject* obj)
            {
                Placement const* place = nullptr;
                Vector3 point;
                PlaceAt(a, 2, place, point, false);

                a.Push(place ? obj->Where().DistanceTo(*place, false)
                             : obj->Where().DistanceTo(point, false));
                return 1;
            }

            /// Centre to centre, sizes ignored.
            int GetExactDistance(Api& a, WorldObject* obj)
            {
                Placement const* place = nullptr;
                Vector3 point;
                PlaceAt(a, 2, place, point, true);

                if (place)
                {
                    if (!obj->Where().ShareFrame(*place))
                    {
                        a.Push(Placement::Unreachable());
                        return 1;
                    }
                    point = place->Pos();
                }

                a.Push((obj->Where().Pos() - point).magnitude());
                return 1;
            }

            int GetExactDistance2d(Api& a, WorldObject* obj)
            {
                Placement const* place = nullptr;
                Vector3 point;
                PlaceAt(a, 2, place, point, false);

                if (place)
                {
                    if (!obj->Where().ShareFrame(*place))
                    {
                        a.Push(Placement::Unreachable());
                        return 1;
                    }
                    point = place->Pos();
                }

                float const dx = obj->Where().X() - point.x;
                float const dy = obj->Where().Y() - point.y;
                a.Push(std::sqrt(dx * dx + dy * dy));
                return 1;
            }

            /**
             * A point @a dist away at @a angle relative to where this is
             * facing.
             *
             * The object's own size is added, so dist is the gap from its
             * edge and not from its centre -- which is what makes a point
             * "just in front of" a large creature actually clear of it.
             */
            int GetRelativePoint(Api& a, WorldObject* obj)
            {
                float const dist = a.Check<float>(2);
                float const angle = a.Check<float>(3);

                Placement const& where = obj->Where();
                Vector3 const point =
                    where.PointAt(dist + where.Extent(), where.Facing() + angle);

                a.Push(point.x);
                a.Push(point.y);
                a.Push(point.z);
                return 3;
            }

            /// The absolute bearing from this object to another or a point,
            /// in radians, 0..2*pi. Facing is not involved.
            int GetAngle(Api& a, WorldObject* obj)
            {
                Placement const* place = nullptr;
                Vector3 point;
                PlaceAt(a, 2, place, point, false);

                a.Push(place ? obj->Where().BearingTo(*place)
                             : obj->Where().BearingTo(point));
                return 1;
            }

            /**
             * Whether two objects are in the same space at all.
             *
             * Not a map-id comparison, which is what Eluna did and what this
             * core deliberately cannot express: a passenger on a ship is on
             * the vessel's own frame, and comparing its map id with the
             * shore's would say "same map" about two things that can never
             * see each other's coordinates.
             */
            int IsInMap(Api& a, WorldObject* obj)
            {
                WorldObject* target = a.CheckObj<WorldObject>(2, false);
                a.Push(target && obj->Where().ShareFrame(target->Where()));
                return 1;
            }

            int IsWithinDist(Api& a, WorldObject* obj)
            {
                WorldObject* target = a.CheckObj<WorldObject>(2);
                float const dist = a.Check<float>(3);
                bool const is3D = a.Check<bool>(4, true);

                a.Push(target &&
                       obj->Where().WithinDist(target->Where(), dist, is3D));
                return 1;
            }

            int IsInRange(Api& a, WorldObject* obj)
            {
                WorldObject* target = a.CheckObj<WorldObject>(2);
                float const minRange = a.Check<float>(3);
                float const maxRange = a.Check<float>(4);
                bool const is3D = a.Check<bool>(5, true);

                a.Push(target && obj->Where().WithinRange(target->Where(),
                                                          minRange, maxRange,
                                                          is3D));
                return 1;
            }

            int IsInFront(Api& a, WorldObject* obj)
            {
                WorldObject* target = a.CheckObj<WorldObject>(2);
                float const dist = a.Check<float>(3);
                float const arc = a.Check<float>(4, Placement::Pi());

                a.Push(target &&
                       obj->Where().IsInFront(target->Where(), dist, arc));
                return 1;
            }

            int IsInBack(Api& a, WorldObject* obj)
            {
                WorldObject* target = a.CheckObj<WorldObject>(2);
                float const dist = a.Check<float>(3);
                float const arc = a.Check<float>(4, Placement::Pi());

                a.Push(target &&
                       obj->Where().IsInBack(target->Where(), dist, arc));
                return 1;
            }

            /// Line of sight, which is a terrain question and therefore the
            /// map's, not the placement's.
            int IsWithinLoS(Api& a, WorldObject* obj)
            {
                Placement const* place = nullptr;
                Vector3 point;
                PlaceAt(a, 2, place, point, true);

                Map* map = obj->GetMap();
                if (!map)
                {
                    a.Push(false);
                    return 1;
                }

                if (place)
                {
                    if (!obj->Where().ShareFrame(*place))
                    {
                        a.Push(false);
                        return 1;
                    }
                    point = place->Pos();
                }

                Placement const& where = obj->Where();
                a.Push(map->IsInLineOfSight(where.X(), where.Y(), where.Z(),
                                            point.x, point.y, point.z));
                return 1;
            }

            int GetNearestPlayer(Api& a, WorldObject* obj)
            {
                float const range = SearchRange(a, 2);

                std::list<Player*> found;
                InRange<Player> check{ obj, range, 0, false };
                MaNGOS::PlayerListSearcher<InRange<Player>> searcher(found,
                                                                     check);
                Cell::VisitAllObjects(obj, searcher, range);

                Player* nearest = nullptr;
                for (Player* candidate : found)
                {
                    if (!nearest ||
                        obj->Where().IsNearer(candidate->Where(),
                                              nearest->Where()))
                    {
                        nearest = candidate;
                    }
                }

                a.Push(nearest);
                return 1;
            }

            int GetNearestCreature(Api& a, WorldObject* obj)
            {
                float const range = SearchRange(a, 2);
                uint32 const entry = a.Check<uint32>(3, 0);

                std::list<Creature*> found;
                InRange<Creature> check{ obj, range, entry, true };
                MaNGOS::CreatureListSearcher<InRange<Creature>> searcher(found,
                                                                         check);
                Cell::VisitAllObjects(obj, searcher, range);

                Creature* nearest = nullptr;
                for (Creature* candidate : found)
                {
                    if (!nearest ||
                        obj->Where().IsNearer(candidate->Where(),
                                              nearest->Where()))
                    {
                        nearest = candidate;
                    }
                }

                a.Push(nearest);
                return 1;
            }

            int GetNearestGameObject(Api& a, WorldObject* obj)
            {
                float const range = SearchRange(a, 2);
                uint32 const entry = a.Check<uint32>(3, 0);

                std::list<GameObject*> found;
                InRange<GameObject> check{ obj, range, entry, false };
                MaNGOS::GameObjectListSearcher<InRange<GameObject>> searcher(
                    found, check);
                Cell::VisitAllObjects(obj, searcher, range);

                GameObject* nearest = nullptr;
                for (GameObject* candidate : found)
                {
                    if (!nearest ||
                        obj->Where().IsNearer(candidate->Where(),
                                              nearest->Where()))
                    {
                        nearest = candidate;
                    }
                }

                a.Push(nearest);
                return 1;
            }

            int GetPlayersInRange(Api& a, WorldObject* obj)
            {
                float const range = SearchRange(a, 2);

                std::list<Player*> found;
                InRange<Player> check{ obj, range, 0, false };
                MaNGOS::PlayerListSearcher<InRange<Player>> searcher(found,
                                                                     check);
                Cell::VisitAllObjects(obj, searcher, range);

                PushList(a, found);
                return 1;
            }

            int GetCreaturesInRange(Api& a, WorldObject* obj)
            {
                float const range = SearchRange(a, 2);
                uint32 const entry = a.Check<uint32>(3, 0);
                bool const aliveOnly = a.Check<bool>(4, true);

                std::list<Creature*> found;
                InRange<Creature> check{ obj, range, entry, aliveOnly };
                MaNGOS::CreatureListSearcher<InRange<Creature>> searcher(found,
                                                                         check);
                Cell::VisitAllObjects(obj, searcher, range);

                PushList(a, found);
                return 1;
            }

            int GetGameObjectsInRange(Api& a, WorldObject* obj)
            {
                float const range = SearchRange(a, 2);
                uint32 const entry = a.Check<uint32>(3, 0);

                std::list<GameObject*> found;
                InRange<GameObject> check{ obj, range, entry, false };
                MaNGOS::GameObjectListSearcher<InRange<GameObject>> searcher(
                    found, check);
                Cell::VisitAllObjects(obj, searcher, range);

                PushList(a, found);
                return 1;
            }

            int SendPacket(Api& a, WorldObject* obj)
            {
                if (WorldPacket* packet = a.CheckObj<WorldPacket>(2))
                {
                    obj->SendMessageToSet(packet, true);
                }
                return 0;
            }

            int SummonGameObject(Api& a, WorldObject* obj)
            {
                uint32 const entry = a.Check<uint32>(2);
                float const x = a.Check<float>(3);
                float const y = a.Check<float>(4);
                float const z = a.Check<float>(5);
                float const o = a.Check<float>(6);
                uint32 const respawnDelay = a.Check<uint32>(7, 30);

                a.Push(obj->SummonGameObject(entry, x, y, z, o, respawnDelay));
                return 1;
            }

            int SpawnCreature(Api& a, WorldObject* obj)
            {
                uint32 const entry = a.Check<uint32>(2);
                float const x = a.Check<float>(3);
                float const y = a.Check<float>(4);
                float const z = a.Check<float>(5);
                float const o = a.Check<float>(6);
                uint32 const spawnType = a.Check<uint32>(7, 8);
                uint32 const despawnTimer = a.Check<uint32>(8, 0);

                if (spawnType > TEMPSPAWN_TIMED_OOC_OR_CORPSE_DESPAWN)
                {
                    luaL_error(a.L, "SpawnCreature: %u is not a spawn type",
                               spawnType);
                }

                a.Push(obj->SummonCreature(entry, x, y, z, o,
                                           TempSpawnType(spawnType),
                                           despawnTimer));
                return 1;
            }

            int PlayMusic(Api& a, WorldObject* obj)
            {
                uint32 const sound = a.Check<uint32>(2);
                obj->PlayMusic(sound, a.CheckObj<Player>(3, false));
                return 0;
            }

            int PlayDirectSound(Api& a, WorldObject* obj)
            {
                uint32 const sound = a.Check<uint32>(2);
                obj->PlayDirectSound(sound, a.CheckObj<Player>(3, false));
                return 0;
            }

            int PlayDistanceSound(Api& a, WorldObject* obj)
            {
                uint32 const sound = a.Check<uint32>(2);
                obj->PlayDistanceSound(sound, a.CheckObj<Player>(3, false));
                return 0;
            }
        }

        MethodEntry const* WorldObjectMethods(std::size_t& count)
        {
#define M(name)                                                               \
            { #name, [](Api& a, void* self) -> int                            \
                { return name(a, static_cast<WorldObject*>(self)); } }

            static MethodEntry const table[] =
            {
                M(GetName),
                M(GetMap),
                M(GetInstanceId),
                M(GetMapId),
                M(GetAreaId),
                M(GetZoneId),
                M(GetX),
                M(GetY),
                M(GetZ),
                M(GetO),
                M(GetLocation),
                M(GetDistance),
                M(GetDistance2d),
                M(GetExactDistance),
                M(GetExactDistance2d),
                M(GetRelativePoint),
                M(GetAngle),
                M(IsInMap),
                M(IsWithinDist),
                M(IsInRange),
                M(IsInFront),
                M(IsInBack),
                M(IsWithinLoS),
                M(GetNearestPlayer),
                M(GetNearestCreature),
                M(GetNearestGameObject),
                M(GetPlayersInRange),
                M(GetCreaturesInRange),
                M(GetGameObjectsInRange),
                M(SendPacket),
                M(SummonGameObject),
                M(SpawnCreature),
                M(PlayMusic),
                M(PlayDirectSound),
                M(PlayDistanceSound),

                // No phasing in 2.4.3. This is a client-version fact and not
                // a gap to be filled: the mechanic arrives in Wrath, and a
                // script asking for it here is asking for something the
                // client would not honour.
                { "GetPhaseMask", nullptr },
                { "SetPhaseMask", nullptr },

                // Eluna's per-object timed callbacks. They need a scheduler
                // that ticks with the map, which this engine does not have
                // yet; they are named here so the failure says that rather
                // than "attempt to call a nil value".
                { "RegisterEvent", nullptr },
                { "RemoveEventById", nullptr },
                { "RemoveEvents", nullptr },
            };

#undef M

            count = sizeof(table) / sizeof(table[0]);
            return table;
        }
    }
}
