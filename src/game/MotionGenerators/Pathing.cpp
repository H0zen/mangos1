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

#include "Pathing.h"

#include "Creature.h"
#include "GridMap.h"
#include "Log.h"
#include "Object.h"        // ClampToAllowedZ
#include "Unit.h"
#include "DisableMgr.h"
#include "World.h"

#include "nav/NavStore.hpp"
#include "nav/Router.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

namespace
{
    /// Maps the config has switched routing off for. Null until the config is read,
    /// which is the same thing as "nothing is excluded".
    std::set<uint32>* g_disabledMaps = nullptr;

    /// How far above or below a given position a walkable surface may be and still be
    /// taken as the one it is standing on, in yards. Generous, because spawns are
    /// routinely a yard or two inside a hillside and a mover that has just been knocked
    /// back is briefly in the air.
    const float SEAT_TOLERANCE = 3.0f;

    /// Length of one segment of a shortcut, in yards.
    const float SHORTCUT_SEGMENT = 5.0f;
}

namespace Nav
{
    namespace Policy
    {
        void PreventOnMaps(const char* ignoreMapIds)
        {
            if (!g_disabledMaps)
            {
                g_disabledMaps = new std::set<uint32>();
            }

            if (!ignoreMapIds)
            {
                return;
            }

            // Parsed without strtok, which is not reentrant and mutates the string it
            // is given -- the reason the version this replaces had to copy the config
            // value onto the heap first.
            std::string list(ignoreMapIds);
            size_t at = 0;
            while (at < list.size())
            {
                const size_t comma = list.find(',', at);
                const std::string piece =
                    list.substr(at, comma == std::string::npos ? std::string::npos
                                                               : comma - at);
                if (!piece.empty())
                {
                    g_disabledMaps->insert(uint32(std::atoi(piece.c_str())));
                }

                if (comma == std::string::npos)
                {
                    break;
                }
                at = comma + 1;
            }
        }

        void Clear()
        {
            delete g_disabledMaps;
            g_disabledMaps = nullptr;
        }

        bool ForceEnabled(const Unit* unit)
        {
            if (const Creature* creature = dynamic_cast<const Creature*>(unit))
            {
                if (const CreatureInfo* info = creature->GetCreatureInfo())
                {
                    return (info->ExtraFlags &
                            CREATURE_FLAG_EXTRA_MMAP_FORCE_ENABLE) != 0;
                }
            }
            return false;
        }

        bool ForceDisabled(const Unit* unit)
        {
            if (const Creature* creature = dynamic_cast<const Creature*>(unit))
            {
                if (const CreatureInfo* info = creature->GetCreatureInfo())
                {
                    return (info->ExtraFlags &
                            CREATURE_FLAG_EXTRA_MMAP_FORCE_DISABLE) != 0;
                }
            }
            return false;
        }

        bool EnabledFor(uint32 mapId, const Unit* unit)
        {
            // Config AND the disables table. The table was being ignored: nothing
            // called DisableMgr::IsPathfindingEnabled, so a map switched off in the
            // database with DISABLE_TYPE_MMAP was routed anyway, and the only way to
            // stop it was the config's own ignore list. Two switches, one of them
            // silently inert.
            if (!DisableMgr::IsPathfindingEnabled(mapId))
            {
                return false;
            }

            if (unit)
            {
                // A client drives its own movement, so a player is always routed: the
                // server's opinion of where he may walk has to match the client's.
                if (unit->GetTypeId() == TYPEID_PLAYER)
                {
                    return true;
                }

                if (ForceDisabled(unit))
                {
                    return false;
                }

                if (ForceEnabled(unit))
                {
                    return true;
                }

                if (unit->GetTypeId() == TYPEID_UNIT &&
                    static_cast<const Creature*>(unit)->IsPet() && unit->GetOwner() &&
                    unit->GetOwner()->GetTypeId() == TYPEID_PLAYER)
                {
                    return true;
                }
            }

            return !g_disabledMaps ||
                   g_disabledMaps->find(mapId) == g_disabledMaps->end();
        }
    }
}

Pathing::Pathing(Unit const* owner) : Pathing(owner, owner->GetMapId())
{
}

Pathing::Pathing(Unit const* owner, uint32 mapId)
    : m_owner(owner), m_mapId(mapId),
      m_routingAllowed(Nav::Policy::EnabledFor(mapId, owner))
{
}

bool Pathing::HasNavigation() const
{
    const Nav::NavStore* store = Nav::NavStores::Instance().Find(m_mapId);
    return store && store->ResidentCount() > 0;
}

bool Pathing::calculate(float destX, float destY, float destZ, bool forceDest,
                        Nav::SearchBudget budget)
{
    return calculate(m_owner->Where().X(), m_owner->Where().Y(),
                     m_owner->Where().Z(), destX, destY, destZ, forceDest, budget);
}

bool Pathing::calculate(float startX, float startY, float startZ, float destX,
                        float destY, float destZ, bool forceDest,
                        Nav::SearchBudget budget)
{
    if (!MaNGOS::IsValidMapCoord(startX, startY, startZ) ||
        !MaNGOS::IsValidMapCoord(destX, destY, destZ))
    {
        return false;
    }

    m_start = Geometry::Vector3(startX, startY, startZ);
    m_end = Geometry::Vector3(destX, destY, destZ);
    m_actualEnd = m_end;

    // Fail-safe, not merely tidy. Every branch below assigns the outcome, but starting
    // from the refusing state means a branch that ever forgets answers "no route"
    // rather than inheriting the previous call's success -- and this object is reused
    // across legs, so the previous call's success is right there to be inherited.
    m_route.Clear();

    // Re-snapshotted per request. The permissions depend on where the mover is standing
    // right now, and this object outlives a single leg.
    m_profile = Nav::ProfileOf(*m_owner);

    if (!m_routingAllowed || m_profile.ignorePathfinding)
    {
        BuildShortcut();
        m_route.outcome = Nav::RouteOutcome::Direct;
        m_route.stop = Nav::RouteStop::NoMesh;
        return true;
    }

    Nav::RouteRequest request;
    request.start = m_start;
    request.end = m_end;
    request.profile = m_profile;
    request.budget = budget;
    request.forceDestination = forceDest;
    request.seatTolerance = SEAT_TOLERANCE;

    const Nav::Router router(Nav::NavStores::Instance().For(m_mapId));
    router.Find(request, m_route);

    if (m_route.UsedGeometry())
    {
        if (!m_route.points.empty())
        {
            m_actualEnd = m_route.points.back();
        }

        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING,
                         "++ Pathing::calculate: %s routed %u points, outcome %d\n",
                         m_owner->GetGuidStr().c_str(),
                         uint32(m_route.points.size()), int(m_route.outcome));
        return true;
    }

    // The router reported a fact -- there is no described ground at one end, or none
    // this mover fits on. What that MEANS is decided here, because it depends on the
    // terrain under the gap and on what the mover is.
    const bool underWater =
        m_owner->GetTerrain()->IsUnderWater(m_start.x, m_start.y, m_start.z) ||
        m_owner->GetTerrain()->IsUnderWater(m_end.x, m_end.y, m_end.z);

    const Nav::RouteStop stop = m_route.stop;
    BuildShortcut();

    if (m_profile.MayGoDirect(underWater) || forceDest)
    {
        m_route.outcome = Nav::RouteOutcome::Direct;
        m_route.stop = forceDest && !m_profile.MayGoDirect(underWater)
                           ? Nav::RouteStop::Forced
                           : stop;
    }
    else
    {
        // The points are laid anyway. A caller that honours Failed() ignores them, and
        // one that does not at least has a line rather than an empty path -- which is
        // the difference between a creature standing still and a creature that walks
        // into the wall it cannot route around and then evades.
        m_route.outcome = Nav::RouteOutcome::Unroutable;
        m_route.stop = stop;
    }

    if (!m_route.points.empty())
    {
        m_actualEnd = m_route.points.back();
    }

    return true;
}

void Pathing::BuildShortcut()
{
    m_route.points.clear();

    const float dx = m_actualEnd.x - m_start.x;
    const float dy = m_actualEnd.y - m_start.y;
    const float dz = m_actualEnd.z - m_start.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    const uint32 segments =
        std::max<uint32>(1, uint32(distance / SHORTCUT_SEGMENT));
    const uint32 count = segments + 1;

    m_route.points.resize(count);
    m_route.points[0] = m_start;
    m_route.points[count - 1] = m_actualEnd;

    for (uint32 i = 1; i < count - 1; ++i)
    {
        const float t = float(i) / float(segments);
        Geometry::Vector3 point = m_start + (m_actualEnd - m_start) * t;
        ClampToAllowedZ(*m_owner, point.x, point.y, point.z);
        m_route.points[i] = point;
    }
}
