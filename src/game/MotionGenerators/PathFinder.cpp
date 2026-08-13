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

#include "Utilities/Errors.h"
#include <algorithm>
#include "../recastnavigation/Detour/Include/DetourCommon.h"

#include "MoveMap.h"
#include "GridDefines.h"   // MaNGOS::IsValidMapCoord
#include "GridMap.h"       // TerrainInfo::IsUnderWater -- Object.h only forward-declares it
#include "Object.h"        // ClampToAllowedZ
#include "Unit.h"
#include "PathFinder.h"
#include "Log.h"

#include <cfloat>
#include <cmath>
#include <cstring>

////////////////// PathFinder //////////////////

namespace
{
    // A yard of water costs a yard of ground times how much longer it takes to cross it:
    // base run is 7.0 yd/s against base swim 4.722 yd/s, so about 1.48. Rounded to 1.5,
    // because the ratio is not exact for every creature and the search does not need it
    // to be -- what it needs is for water to stop being free.
    const float PATH_COST_SWIM = 1.5f;

    // Magma and slime stay REACHABLE and merely expensive. Creatures take no
    // environmental damage and their include mask has always cleared them to swim in
    // both, so excluding them here would strand anything whose target genuinely sits in
    // lava -- a fire elemental pulled into its own pool. At 5.0 a detour wins until it
    // is five times longer, and where no detour exists the path is still found.
    const float PATH_COST_HAZARD = 5.0f;
}

/**
 * @brief Constructor for PathFinder.
 * @param owner The unit that owns this PathFinder.
 */
PathFinder::PathFinder(const Unit* owner) :
    PathFinder(owner, owner->GetMapId())
{
}

PathFinder::PathFinder(const Unit* owner, uint32 mapId) :
    m_useStraightPath(false), m_forceDestination(false),
    m_budgetStop(RouteStop::Reached),
    m_sourceUnit(owner), m_navMesh(NULL), m_navMeshQuery(NULL)
{
    DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::PathInfo for %u \n", m_sourceUnit->GetGUIDLow());

    if (MMAP::MMapFactory::IsPathfindingEnabled(mapId, owner))
    {
        MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
        m_navMesh = mmap->GetNavMesh(mapId);
        m_navMeshQuery = mmap->GetNavMeshQuery(mapId, m_sourceUnit->GetInstanceId());
    }

    m_profile = ProfileOf(*m_sourceUnit);
    applyFilter();
}

/**
 * @brief Destructor for PathFinder.
 */
PathFinder::~PathFinder()
{
    DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::~PathInfo() for %u \n", m_sourceUnit->GetGUIDLow());
}

/**
 * @brief Calculates the path from the source unit to the destination.
 * @param destX The X-coordinate of the destination.
 * @param destY The Y-coordinate of the destination.
 * @param destZ The Z-coordinate of the destination.
 * @param forceDest Whether to force the destination.
 * @return True if the path was successfully calculated, false otherwise.
 */
bool PathFinder::calculate(float destX, float destY, float destZ, bool forceDest,
                           SearchBudget budget)
{
    float x, y, z;
    x = m_sourceUnit->Where().X();
    y = m_sourceUnit->Where().Y();
    z = m_sourceUnit->Where().Z();

    return calculate(x, y, z, destX, destY, destZ, forceDest, budget);
}

/**
 * @brief Calculates the path from an explicit start position to the destination.
 * @param startX The X-coordinate of the start position.
 * @param startY The Y-coordinate of the start position.
 * @param startZ The Z-coordinate of the start position.
 * @param destX The X-coordinate of the destination.
 * @param destY The Y-coordinate of the destination.
 * @param destZ The Z-coordinate of the destination.
 * @param forceDest Whether to force the destination.
 * @return True if the path was successfully calculated, false otherwise.
 */
bool PathFinder::calculate(float startX, float startY, float startZ, float destX, float destY, float destZ, bool forceDest,
                           SearchBudget budget)
{
    if (!MaNGOS::IsValidMapCoord(startX, startY, startZ) ||
        !MaNGOS::IsValidMapCoord(destX, destY, destZ))
    {
        return false;
    }

    Vector3 start(startX, startY, startZ);
    setStartPosition(start);

    Vector3 dest(destX, destY, destZ);
    setEndPosition(dest);

    m_forceDestination = forceDest;
    m_budget = budget;

    // Fail-safe, not merely tidy. Every branch below assigns the outcome, but starting
    // from the refusing state means a branch that ever forgets to answers "no route"
    // rather than inheriting the previous call's success -- and this object is reused
    // across legs, so the previous call's success is right there to be inherited.
    m_route.outcome = RouteOutcome::Unroutable;
    m_route.stop = RouteStop::Failed;
    m_budgetStop = RouteStop::Reached;

    // Re-snapshotted per request, not per router. The permissions depend on where the
    // mover is standing right now, and this object outlives a single leg.
    m_profile = ProfileOf(*m_sourceUnit);
    applyFilter();

    DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::calculate() for %u \n", m_sourceUnit->GetGUIDLow());

    // make sure navMesh works - we can run on map w/o mmap
    // check if the start and end point have a .mmtile loaded (can we pass via not loaded tile on the way?)
    if (!m_navMesh || !m_navMeshQuery || m_profile.ignorePathfinding ||
        !HaveTile(start) || !HaveTile(dest))
    {
        BuildShortcut();
        m_route.outcome = RouteOutcome::Direct;
        m_route.stop = RouteStop::NoMesh;
        return true;
    }

    BuildPolyPath(start, dest);
    return true;
}

/**
 * @brief Gets the nearest polygon reference by position.
 * @param polyPath The polygon path.
 * @param polyPathSize The size of the polygon path.
 * @param point The point to find the nearest polygon for.
 * @param distance The distance to the nearest polygon.
 * @return The nearest polygon reference.
 */
dtPolyRef PathFinder::getPathPolyByPosition(const dtPolyRef* polyPath, uint32 polyPathSize, const float* point, float* distance) const
{
    if (!polyPath || !polyPathSize)
    {
        return INVALID_POLYREF;
    }

    dtPolyRef nearestPoly = INVALID_POLYREF;
    float minDist2d = FLT_MAX;
    float minDist3d = 0.0f;

    for (uint32 i = 0; i < polyPathSize; ++i)
    {
        float closestPoint[VERTEX_SIZE];
        dtStatus dtResult = m_navMeshQuery->closestPointOnPoly(polyPath[i], point, closestPoint, NULL);
        if (dtStatusFailed(dtResult))
        {
            continue;
        }

        float d = dtVdist2DSqr(point, closestPoint);
        if (d < minDist2d)
        {
            minDist2d = d;
            nearestPoly = polyPath[i];
            minDist3d = dtVdistSqr(point, closestPoint);
        }

        if (minDist2d < 1.0f) // shortcut out - close enough for us
        {
            break;
        }
    }

    if (distance)
    {
        *distance = dtMathSqrtf(minDist3d);
    }

    return (minDist2d < 3.0f) ? nearestPoly : INVALID_POLYREF;
}

/**
 * @brief Gets the polygon reference by location.
 * @param point The point to find the polygon for.
 * @param distance The distance to the polygon.
 * @return The polygon reference.
 */
dtPolyRef PathFinder::getPolyByLocation(const float* point, float* distance) const
{
    // first we check the current path
    // if the current path doesn't contain the current poly,
    // we need to use the expensive navMesh.findNearestPoly
    dtPolyRef polyRef = getPathPolyByPosition(m_corridor.Polys(), m_corridor.Length(), point, distance);
    if (polyRef != INVALID_POLYREF)
    {
        return polyRef;
    }

    // we don't have it in our old path
    // try to get it by findNearestPoly()
    // first try with low search box
    float extents[VERTEX_SIZE] = {3.0f, 5.0f, 3.0f};    // bounds of poly search area
    float closestPoint[VERTEX_SIZE] = {0.0f, 0.0f, 0.0f};
    dtStatus dtResult = m_navMeshQuery->findNearestPoly(point, extents, &m_filter, &polyRef, closestPoint);
    if (dtStatusSucceed(dtResult) && polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }

    // still nothing ..
    // try with bigger search box
    extents[1] = 200.0f;
    dtResult = m_navMeshQuery->findNearestPoly(point, extents, &m_filter, &polyRef, closestPoint);
    if (dtStatusSucceed(dtResult) && polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }

    return INVALID_POLYREF;
}

/**
 * @brief Tries to replace the whole search with the straight segment.
 * @param startPoly Polygon the start position sits on.
 * @param endPoly Polygon the end position sits on.
 * @param startPoint Start position, in Detour's axis order.
 * @param endPoint End position, in Detour's axis order.
 * @return True when the segment is walkable and provably optimal.
 */
bool PathFinder::BuildStraightShortcut(dtPolyRef startPoly, dtPolyRef endPoly,
                                       const float* startPoint, const float* endPoint)
{
    float t = 0.0f;
    float hitNormal[VERTEX_SIZE] = {0.0f, 0.0f, 0.0f};
    dtPolyRef path[Corridor::CAPACITY];
    int pathLength = 0;

    dtStatus dtResult = m_navMeshQuery->raycast(startPoly, startPoint, endPoint,
                                                &m_filter, &t, hitNormal,
                                                path, &pathLength, int(Corridor::CAPACITY));

    // A truncated corridor is rejected rather than trusted: past the buffer the ray
    // keeps travelling but stops recording, so the last polygon written is no longer
    // the last one crossed and the end test below would be answered about the wrong one.
    if (dtStatusFailed(dtResult) || dtStatusDetail(dtResult, DT_BUFFER_TOO_SMALL) ||
        pathLength <= 0)
    {
        return false;
    }

    // t is how much of the segment was walked before a wall stopped it, and Detour
    // reports FLT_MAX when nothing did. Any finite value means the straight line leaves
    // the walkable surface, so there is nothing to shortcut.
    if (t < FLT_MAX)
    {
        return false;
    }

    // Reaching the destination polygon is a separate condition from crossing no wall:
    // the ray also ends when it runs off the loaded mesh into a hole, which passes the
    // test above and would leave the point path aiming at a polygon nothing reached.
    if (path[pathLength - 1] != endPoly)
    {
        return false;
    }

    // The one condition that makes this shortcut SOUND rather than merely fast.
    //
    // A straight line is the cheapest route only where every yard of it is priced the
    // same. The filter now charges more for water than for ground, so a segment that
    // crosses a shoreline can cost more than a longer detour that stays dry -- and
    // dtNavMeshQuery::raycast knows nothing of cost, so it would answer "walkable" with
    // full confidence and quietly return a worse path than the search it replaced.
    // Demanding one price along the whole corridor is what keeps the two in agreement;
    // where the price changes, the search runs and decides properly.
    //
    // Compared exactly, and deliberately: both sides are read out of the same
    // m_areaCost table, so equal areas give bit-identical floats and there is no
    // rounding to tolerate. Two different areas priced the same -- magma and slime --
    // are genuinely interchangeable here, which is why the cost is compared and not the
    // area id.
    unsigned char firstArea = 0;
    if (dtStatusFailed(m_navMesh->getPolyArea(path[0], &firstArea)))
    {
        return false;
    }

    const float uniformCost = m_filter.getAreaCost(firstArea);
    for (int i = 1; i < pathLength; ++i)
    {
        unsigned char area = 0;
        if (dtStatusFailed(m_navMesh->getPolyArea(path[i], &area)) ||
            m_filter.getAreaCost(area) != uniformCost)
        {
            return false;
        }
    }

    m_corridor.Assign(path, uint32(pathLength));

    DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING,
                     "++ PathFinder::BuildStraightShortcut :: %u walks the segment "
                     "across %d polygons, no search needed\n",
                     m_sourceUnit->GetGUIDLow(), pathLength);
    return true;
}

/**
 * @brief Records that a Detour search stopped at a budget rather than at the world.
 * @param status The status returned by the Detour query.
 * @param where Name of the call site, for the log line.
 */
void PathFinder::noteSearchLimit(dtStatus status, const char* where)
{
    // First cause wins: a search that exhausted its nodes and then filled the buffer is
    // explained by the first thing that stopped it, not the last symptom of it.
    if (dtStatusDetail(status, DT_OUT_OF_NODES))
    {
        if (m_budgetStop == RouteStop::Reached)
        {
            m_budgetStop = RouteStop::NodeBudget;
        }
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING,
                         "++ PathFinder::%s :: %u exhausted the search node pool (%d); "
                         "the path is short for that reason, not because the way is "
                         "blocked\n",
                         where, m_sourceUnit->GetGUIDLow(), MMAP::MMAP_QUERY_MAX_NODES);
    }

    if (dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
    {
        if (m_budgetStop == RouteStop::Reached)
        {
            m_budgetStop = RouteStop::PolyBudget;
        }
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING,
                         "++ PathFinder::%s :: %u filled the polygon buffer (%d); the "
                         "route is longer than one path may describe\n",
                         where, m_sourceUnit->GetGUIDLow(), int(Corridor::CAPACITY));
    }
}

/**
 * @brief Builds the polygon path from the start position to the end position.
 * @param startPos The start position.
 * @param endPos The end position.
 */
void PathFinder::BuildPolyPath(const Vector3& startPos, const Vector3& endPos)
{
    // *** getting start/end poly logic ***

    float distToStartPoly, distToEndPoly;
    float startPoint[VERTEX_SIZE] = {startPos.y, startPos.z, startPos.x};
    float endPoint[VERTEX_SIZE] = {endPos.y, endPos.z, endPos.x};

    dtPolyRef startPoly = getPolyByLocation(startPoint, &distToStartPoly);
    dtPolyRef endPoly = getPolyByLocation(endPoint, &distToEndPoly);

    dtStatus dtResult;

    // we have a hole in our mesh
    // make shortcut path and mark it as NOPATH ( with flying exception )
    // its up to caller how he will use this info
    if (startPoly == INVALID_POLYREF || endPoly == INVALID_POLYREF)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPoly == 0 || endPoly == 0)\n");
        BuildShortcut();

        const bool offMeshUnderWater =
            (startPoly == INVALID_POLYREF &&
             m_sourceUnit->GetTerrain()->IsUnderWater(startPos.x, startPos.y, startPos.z)) ||
            (endPoly == INVALID_POLYREF &&
             m_sourceUnit->GetTerrain()->IsUnderWater(endPos.x, endPos.y, endPos.z));

        m_route.outcome = m_profile.MayGoDirect(offMeshUnderWater) ? RouteOutcome::Direct
                                                         : RouteOutcome::Unroutable;
        m_route.stop = RouteStop::OffMesh;
        return;
    }

    // we may need a better number here
    bool farFromPoly = (distToStartPoly > 7.0f || distToEndPoly > 7.0f);
    if (farFromPoly)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: farFromPoly distToStartPoly=%.3f distToEndPoly=%.3f\n", distToStartPoly, distToEndPoly);

        const Vector3 p = (distToStartPoly > 7.0f) ? startPos : endPos;
        const bool underWater = m_sourceUnit->GetTerrain()->IsUnderWater(p.x, p.y, p.z);
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: %s case\n",
                         underWater ? "underWater" : "flying");

        if (m_profile.MayGoDirect(underWater))
        {
            BuildShortcut();
            m_route.outcome = RouteOutcome::Direct;
            m_route.stop = RouteStop::OffMesh;
            return;
        }

        float closestPoint[VERTEX_SIZE];
        // we may want to use closestPointOnPolyBoundary instead
        dtResult = m_navMeshQuery->closestPointOnPoly(endPoly, endPoint, closestPoint, NULL);
        if (dtStatusSucceed(dtResult))
        {
            dtVcopy(endPoint, closestPoint);
            setActualEndPosition(Vector3(endPoint[2], endPoint[0], endPoint[1]));
        }
    }

    // *** poly path generating logic ***

    // start and end are on same polygon
    // just need to move in straight line
    if (startPoly == endPoly)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPoly == endPoly)\n");

        BuildShortcut();

        m_corridor.Assign(&startPoly, 1);

        m_route.outcome = farFromPoly ? RouteOutcome::Partial : RouteOutcome::Routed;
        m_route.stop = farFromPoly ? RouteStop::Wall : RouteStop::Reached;
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: outcome %d\n",
                         int(m_route.outcome));
        return;
    }

    // Before searching at all: can the mover simply walk the segment? A ray along the
    // surface costs the polygons it crosses, where A* costs the region it has to
    // explore, and on open ground -- which is most of the world and most of the calls --
    // the ray succeeds. The case above is the same question answered for one polygon;
    // this is it answered for a corridor of them.
    if (BuildStraightShortcut(startPoly, endPoly, startPoint, endPoint))
    {
        m_route.outcome = farFromPoly ? RouteOutcome::Partial : RouteOutcome::Routed;
        m_route.stop = farFromPoly ? RouteStop::Wall : RouteStop::Reached;
        BuildPointPath(startPoint, endPoint);
        return;
    }

    // look for startPoly/endPoly in current path
    // here to catch few bugs
    MANGOS_ASSERT(!m_corridor.HasInvalid() ||
                  m_sourceUnit->PrintEntryError("PathFinder::BuildPolyPath"));

    // Where the mover has got to, and how much of the old route still leads to the
    // goal. FindLastAfter takes the LAST occurrence deliberately -- a corridor that
    // doubles back round an obstacle visits a polygon twice, and cutting at the first
    // visit throws away the half that goes somewhere.
    const uint32 startIndex = m_corridor.Find(startPoly);
    const uint32 endIndex = m_corridor.FindLastAfter(endPoly, startIndex);
    const bool startPolyFound = (startIndex != Corridor::NPOS);
    const bool endPolyFound = (endIndex != Corridor::NPOS);

    if (startPolyFound && endPolyFound)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPolyFound && endPolyFound)\n");

        // we moved along the path and the target did not move out of our old poly-path
        // our path is a simple subpath case, we have all the data we need
        // just "cut" it out

        m_corridor.Advance(startIndex);
        m_corridor.Truncate(endIndex - startIndex + 1);
    }
    else if (startPolyFound && !endPolyFound)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPolyFound && !endPolyFound)\n");

        // we are moving on the old path but target moved out
        // so we have atleast part of poly-path ready

        m_corridor.Advance(startIndex);

        // try to adjust the suffix of the path instead of recalculating entire length
        // at given interval the target can not get too far from its last location
        // thus we have less poly to cover
        // sub-path of optimal path is optimal

        // take ~80% of the original length
        // TODO : play with the values here
        uint32 prefixPolyLength = uint32(m_corridor.Length() * 0.8f + 0.5f);

        dtPolyRef suffixStartPoly = m_corridor.At(prefixPolyLength - 1);

        // we need any point on our suffix start poly to generate poly-path, so we need last poly in prefix data
        float suffixEndPoint[VERTEX_SIZE];
        dtResult = m_navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint, NULL);
        if (dtStatusFailed(dtResult))
        {
            // we can hit offmesh connection as last poly - closestPointOnPoly() don't like that
            // try to recover by using prev polyref
            //
            // Only when there IS a previous one. With a single-polygon prefix the
            // decrement below reached zero and the subscript underflowed to
            // 0xFFFFFFFF, reading the corridor array a long way past its end.
            if (prefixPolyLength < 2)
            {
                BuildShortcut();
                m_route.stop = RouteStop::Failed;
                return;
            }

            --prefixPolyLength;
            suffixStartPoly = m_corridor.At(prefixPolyLength - 1);
            dtResult = m_navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint, NULL);
            if (dtStatusFailed(dtResult))
            {
                // suffixStartPoly is still invalid, error state
                BuildShortcut();
                m_route.stop = RouteStop::Failed;
                return;
            }
        }

        // generate suffix. It is written OVER the prefix's last polygon, which is the
        // suffix's first: the two overlap by exactly one, which is why the lengths are
        // added and one subtracted below.
        int suffixPolyLength = 0;
        dtResult = m_navMeshQuery->findPath(
                       suffixStartPoly,    // start polygon
                       endPoly,            // end polygon
                       suffixEndPoint,     // start position
                       endPoint,           // end position
                       &m_filter,            // polygon search filter
                       m_corridor.Buffer() + prefixPolyLength - 1,    // [out] path
                       &suffixPolyLength,
                       int(Corridor::CAPACITY - prefixPolyLength)); // max polygons out

        noteSearchLimit(dtResult, "BuildPolyPath(suffix)");

        if (!suffixPolyLength || dtStatusFailed(dtResult))
        {
            // this is probably an error state, but we'll leave it
            // and hopefully recover on the next Update
            // we still need to copy our preffix
            sLog.outError("%u's Path Build failed: suffix length %d, status 0x%08x",
                          m_sourceUnit->GetGUIDLow(), suffixPolyLength, dtResult);
        }

        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++  corridor=%u prefixPolyLength=%u suffixPolyLength=%d \n", m_corridor.Length(), prefixPolyLength, suffixPolyLength);

        // new path = prefix + suffix - overlap
        m_corridor.SetLength(prefixPolyLength + uint32(suffixPolyLength) - 1);

        // A one-polygon prefix and an empty suffix leave nothing behind, and the
        // verdict below reads the corridor's last polygon. The arithmetic above
        // underflowed to 0xFFFFFFFF and that read went far off the end; the error was
        // already logged just above, so this only stops it being logged and then
        // acted upon.
        if (m_corridor.Empty())
        {
            BuildShortcut();
            m_route.stop = RouteStop::Failed;
            return;
        }
    }
     else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (!startPolyFound && !endPolyFound)\n");

        // either we have no path at all -> first run
        // or something went really wrong -> we aren't moving along the path to the target

        // just generate new path

        // free and invalidate old path data
        clear();

        int polyLength = 0;
        dtResult = m_navMeshQuery->findPath(
                       startPoly,          // start polygon
                       endPoly,            // end polygon
                       startPoint,         // start position
                       endPoint,           // end position
                       &m_filter,           // polygon search filter
                       m_corridor.Buffer(),// [out] path
                       &polyLength,
                       int(Corridor::CAPACITY)); // max number of polygons in output path
        m_corridor.SetLength(uint32(polyLength));

        noteSearchLimit(dtResult, "BuildPolyPath");

        if (!polyLength || dtStatusFailed(dtResult))
        {
            // only happens if we passed bad data to findPath(), or navmesh is messed up
            sLog.outError("%u's Path Build failed: 0 length path, status 0x%08x",
                          m_sourceUnit->GetGUIDLow(), dtResult);
            BuildShortcut();
            m_route.stop = RouteStop::Failed;
            return;
        }
    }

    // by now we know what kind of route we have.
    //
    // `farFromPoly` in place of a re-read of the type: what the old test asked was
    // whether the far-from-polygon branch above had run, and it wrote its answer into
    // the same field being decided here. On a REUSED router that read the previous
    // call's verdict whenever the branch had not run this time, so a route that did
    // reach its goal was demoted to partial because an earlier one had not.
    if (m_corridor.Last() == endPoly && !farFromPoly)
    {
        m_route.outcome = RouteOutcome::Routed;
        m_route.stop = RouteStop::Reached;
    }
    else
    {
        m_route.outcome = RouteOutcome::Partial;
        m_route.stop = (m_budgetStop != RouteStop::Reached) ? m_budgetStop
                                                            : RouteStop::Wall;
    }

    // generate the point-path out of our up-to-date poly-path
    BuildPointPath(startPoint, endPoint);
}

/**
 * @brief Builds the point path from the start point to the end point.
 * @param startPoint The start point.
 * @param endPoint The end point.
 */
void PathFinder::BuildPointPath(const float* startPoint, const float* endPoint)
{
    float pathPoints[Path::MAX_POINTS * VERTEX_SIZE];
    uint32 pointCount = 0;
    dtStatus dtResult = DT_FAILURE;
    if (m_useStraightPath)
    {
        dtResult = m_navMeshQuery->findStraightPath(
                       startPoint,         // start position
                       endPoint,           // end position
                       m_corridor.Polys(), // current path
                       m_corridor.Length(),// length of current path
                       pathPoints,         // [out] path corner points
                       NULL,               // [out] flags
                       NULL,               // [out] shortened path
                       (int*)&pointCount,
                       int(m_budget.points)); // maximum number of points to use
    }
    else
    {
        dtResult = findSmoothPath(
                       startPoint,         // start position
                       endPoint,           // end position
                       m_corridor.Polys(), // current path
                       m_corridor.Length(),// length of current path
                       pathPoints,         // [out] path corner points
                       (int*)&pointCount,
                       m_budget.points);      // maximum number of points
    }

    if (pointCount < 2 || dtStatusFailed(dtResult))
    {
        // only happens if pass bad data to findStraightPath or navmesh is broken
        // single point paths can be generated here
        // TODO : check the exact cases
        DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::BuildPointPath FAILED! path sized %d returned\n", pointCount);
        BuildShortcut();
        m_route.outcome = RouteOutcome::Unroutable;
        m_route.stop = RouteStop::Failed;
        return;
    }

    m_route.points.resize(pointCount);
    for (uint32 i = 0; i < pointCount; ++i)
    {
        m_route.points[i] = Vector3(pathPoints[i * VERTEX_SIZE + 2], pathPoints[i * VERTEX_SIZE], pathPoints[i * VERTEX_SIZE + 1]);
    }

    // first point is always our current location - we need the next one
    setActualEndPosition(m_route.points[pointCount - 1]);

    // force the given destination, if needed
    if (m_forceDestination &&
        (!m_route.IsRouted() || !inRange(getEndPosition(), getActualEndPosition(), 1.0f, 1.0f)))
    {
        // we may want to keep partial subpath
        if (dist3DSqr(getActualEndPosition(), getEndPosition()) <
            0.3f * dist3DSqr(getStartPosition(), getEndPosition()))
        {
            setActualEndPosition(getEndPosition());
            m_route.points[m_route.points.size() - 1] = getEndPosition();
        }
        else
        {
            setActualEndPosition(getEndPosition());
            BuildShortcut();
        }

        // The caller demanded this exact point, so the geometry no longer decides
        // whether it is reached -- it is, by fiat. Direct rather than Routed, because
        // the last leg of it is not geometry any more.
        m_route.outcome = RouteOutcome::Direct;
        m_route.stop = RouteStop::Forced;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::BuildPointPath outcome %d size %d poly-size %d\n", int(m_route.outcome), pointCount, m_corridor.Length());
}

/**
 * @brief Builds a shortcut path directly from the start position to the end position.
 */
void PathFinder::BuildShortcut()
{
    DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::BuildShortcut :: making shortcut for %s\n", m_sourceUnit->GetGuidStr().c_str());

    clear();

    Vector3 start = getStartPosition();
    Vector3 end = getActualEndPosition();

    // Subdivide the shortcut into segments so that each point can be snapped
    // to terrain height. This prevents the "bunny hop" effect on steep
    // slopes when no navmesh is available.
    const float segmentLength = 5.0f;
    float dist = sqrt(dist3DSqr(start, end));
    uint32 segments = std::max(1u, uint32(dist / segmentLength));
    uint32 size = segments + 1;

    m_route.points.resize(size);
    m_route.points[0] = start;
    m_route.points[size - 1] = end;

    for (uint32 i = 1; i < size - 1; ++i)
    {
        float t = float(i) / float(segments);
        Vector3 point = start + (end - start) * t;
        ClampToAllowedZ(*m_sourceUnit, point.x, point.y, point.z);
        m_route.points[i] = point;
    }

    // No outcome written here on purpose -- see the declaration. The same line of points
    // is a legitimate route for a flier and a refusal for a walker, and only the caller
    // knows which mover it is laying them for.
}

/**
 * @brief Pushes the current profile's permissions and the area costs into the filter.
 */
void PathFinder::applyFilter()
{
    m_filter.setIncludeFlags(m_profile.includeFlags);
    m_filter.setExcludeFlags(m_profile.excludeFlags);

    // What a surface COSTS, which is a different question from whether the mover is
    // allowed on it -- and the only one the include mask above cannot express. Every
    // area used to cost the Detour default of 1.0, so A* measured a swim in yards
    // exactly like a run, and any lake shorter than the shore around it won every time.
    //
    // GROUND is the unit and must remain the cheapest. Detour scores a node with
    // dtVdist(pos, endPos) * H_SCALE, H_SCALE being 0.999 -- a plain distance in yards.
    // That heuristic underestimates the true remaining cost, which is what makes A*
    // return the optimal path, only while no area is crossed for less than one unit per
    // yard. An area cost below 1.0 would not make its surface preferred; it would make
    // the search wrong.
    m_filter.setAreaCost(NAV_GROUND, 1.0f);
    m_filter.setAreaCost(NAV_WATER, PATH_COST_SWIM);
    m_filter.setAreaCost(NAV_MAGMA, PATH_COST_HAZARD);
    m_filter.setAreaCost(NAV_SLIME, PATH_COST_HAZARD);
}

/**
 * @brief Checks if the specified point has a tile in the navigation mesh.
 * @param p The point to check.
 * @return True if the point has a tile, false otherwise.
 */
bool PathFinder::HaveTile(const Vector3& p) const
{
    int tx, ty;
    float point[VERTEX_SIZE] = {p.y, p.z, p.x};

    m_navMesh->calcTileLoc(point, &tx, &ty);
    return (m_navMesh->getTileAt(tx, ty, 0) != NULL);
}

/**
 * @brief Fixes up the corridor path by concatenating the visited path with the current path.
 * @param path The current path.
 * @param npath The number of polygons in the current path.
 * @param maxPath The maximum number of polygons in the path.
 * @param visited The visited path.
 * @param nvisited The number of polygons in the visited path.
 * @return The number of polygons in the fixed-up path.
 */
uint32 PathFinder::fixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath,
                                 const dtPolyRef* visited, uint32 nvisited)
{
    int32 furthestPath = -1;
    int32 furthestVisited = -1;

    // Find furthest common polygon.
    for (int32 i = npath - 1; i >= 0; --i)
    {
        bool found = false;
        for (int32 j = nvisited - 1; j >= 0; --j)
        {
            if (path[i] == visited[j])
            {
                furthestPath = i;
                furthestVisited = j;
                found = true;
            }
        }
        if (found)
        {
            break;
        }
    }

    // If no intersection found just return current path.
    if (furthestPath == -1 || furthestVisited == -1)
    {
        return npath;
    }

    // Concatenate paths.

    // Adjust beginning of the buffer to include the visited.
    uint32 req = nvisited - furthestVisited;
    uint32 orig = uint32(furthestPath + 1) < npath ? furthestPath + 1 : npath;
    uint32 size = npath > orig ? npath - orig : 0;
    if (req + size > maxPath)
    {
        size = maxPath - req;
    }

    if (size)
    {
        memmove(path + req, path + orig, size * sizeof(dtPolyRef));
    }

    // Store visited
    for (uint32 i = 0; i < req; ++i)
    {
        path[i] = visited[(nvisited - 1) - i];
    }

    return req + size;
}

/**
 * @brief Gets the steer target for the path.
 * @param startPos The start position.
 * @param endPos The end position.
 * @param minTargetDist The minimum target distance.
 * @param path The path.
 * @param pathSize The size of the path.
 * @param steerPos The steer position.
 * @param steerPosFlag The steer position flag.
 * @param steerPosRef The steer position reference.
 * @return True if the steer target was successfully obtained, false otherwise.
 */
bool PathFinder::getSteerTarget(const float* startPos, const float* endPos,
                                float minTargetDist, const dtPolyRef* path, uint32 pathSize,
                                float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef)
{
    // Find steer target.
    static const uint32 MAX_STEER_POINTS = 3;
    float steerPath[MAX_STEER_POINTS * VERTEX_SIZE];
    unsigned char steerPathFlags[MAX_STEER_POINTS];
    dtPolyRef steerPathPolys[MAX_STEER_POINTS];
    uint32 nsteerPath = 0;
    dtStatus dtResult = m_navMeshQuery->findStraightPath(startPos, endPos, path, pathSize,
                        steerPath, steerPathFlags, steerPathPolys, (int*)&nsteerPath, MAX_STEER_POINTS);
    if (!nsteerPath || dtStatusFailed(dtResult))
    {
        return false;
    }

    // Find vertex far enough to steer to.
    uint32 ns = 0;
    while (ns < nsteerPath)
    {
        // Stop at Off-Mesh link or when point is further than slop away.
        if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
            !inRangeYZX(&steerPath[ns * VERTEX_SIZE], startPos, minTargetDist, 1000.0f))
        {
            break;
        }
        ++ns;
    }
    // Failed to find good point to steer to.
    if (ns >= nsteerPath)
    {
        return false;
    }

    dtVcopy(steerPos, &steerPath[ns * VERTEX_SIZE]);
    steerPos[1] = startPos[1];  // keep Z value
    steerPosFlag = steerPathFlags[ns];
    steerPosRef = steerPathPolys[ns];

    return true;
}

/**
 * @brief Finds a smooth path from the start position to the end position.
 * @param startPos The start position.
 * @param endPos The end position.
 * @param polyPath The polygon path.
 * @param polyPathSize The size of the polygon path.
 * @param smoothPath The smooth path.
 * @param smoothPathSize The size of the smooth path.
 * @param maxSmoothPathSize The maximum size of the smooth path.
 * @return The status of the pathfinding operation.
 */
dtStatus PathFinder::findSmoothPath(const float* startPos, const float* endPos,
                                    const dtPolyRef* polyPath, uint32 polyPathSize,
                                    float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize)
{
    *smoothPathSize = 0;
    uint32 nsmoothPath = 0;

    dtPolyRef polys[Corridor::CAPACITY];
    memcpy(polys, polyPath, sizeof(dtPolyRef)*polyPathSize);
    uint32 npolys = polyPathSize;

    float iterPos[VERTEX_SIZE], targetPos[VERTEX_SIZE];
    dtStatus dtResult = m_navMeshQuery->closestPointOnPolyBoundary(polys[0], startPos, iterPos);
    if (dtStatusFailed(dtResult))
    {
        return DT_FAILURE;
    }

    dtResult = m_navMeshQuery->closestPointOnPolyBoundary(polys[npolys - 1], endPos, targetPos);
    if (dtStatusFailed(dtResult))
    {
        return DT_FAILURE;
    }

    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
    ++nsmoothPath;

    // Move towards target a small advancement at a time until target reached or
    // when ran out of memory to store the path.
    while (npolys && nsmoothPath < maxSmoothPathSize)
    {
        // Find location to steer towards.
        float steerPos[VERTEX_SIZE];
        unsigned char steerPosFlag;
        dtPolyRef steerPosRef = INVALID_POLYREF;

        if (!getSteerTarget(iterPos, targetPos, Path::SMOOTH_SLOP, polys, npolys, steerPos, steerPosFlag, steerPosRef))
        {
            break;
        }

        bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END);
        bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION);

        // Find movement delta.
        float delta[VERTEX_SIZE];
        dtVsub(delta, steerPos, iterPos);
        float len = dtMathSqrtf(dtVdot(delta, delta));
        // If the steer target is end of path or off-mesh link, do not move past the location.
        if ((endOfPath || offMeshConnection) && len < Path::SMOOTH_STEP)
        {
            len = 1.0f;
        }
        else
        {
            len = Path::SMOOTH_STEP / len;
        }

        float moveTgt[VERTEX_SIZE];
        dtVmad(moveTgt, iterPos, delta, len);

        // Move
        float result[VERTEX_SIZE];
        const static uint32 MAX_VISIT_POLY = 16;
        dtPolyRef visited[MAX_VISIT_POLY];

        uint32 nvisited = 0;
        m_navMeshQuery->moveAlongSurface(polys[0], iterPos, moveTgt, &m_filter, result, visited, (int*)&nvisited, MAX_VISIT_POLY);
        npolys = fixupCorridor(polys, npolys, Corridor::CAPACITY, visited, nvisited);

        m_navMeshQuery->getPolyHeight(polys[0], result, &result[1]);
        result[1] += 0.5f;
        dtVcopy(iterPos, result);

        // Handle end of path and off-mesh links when close enough.
        if (endOfPath && inRangeYZX(iterPos, steerPos, Path::SMOOTH_SLOP, Path::SMOOTH_HEIGHT))
        {
            // Reached end of path.
            dtVcopy(iterPos, targetPos);
            if (nsmoothPath < maxSmoothPathSize)
            {
                dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                ++nsmoothPath;
            }
            break;
        }
        else if (offMeshConnection && inRangeYZX(iterPos, steerPos, Path::SMOOTH_SLOP, Path::SMOOTH_HEIGHT))
        {
            // Advance the path up to and over the off-mesh connection.
            dtPolyRef prevRef = INVALID_POLYREF;
            dtPolyRef polyRef = polys[0];
            uint32 npos = 0;
            while (npos < npolys && polyRef != steerPosRef)
            {
                prevRef = polyRef;
                polyRef = polys[npos];
                ++npos;
            }

            for (uint32 i = npos; i < npolys; ++i)
            {
                polys[i - npos] = polys[i];
            }

            npolys -= npos;

            // Handle the connection.
            float newStartPos[VERTEX_SIZE], newEndPos[VERTEX_SIZE];
            // Get the endpoints of the off-mesh connection.
            dtResult = m_navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef, newStartPos, newEndPos);
            if (dtStatusSucceed(dtResult))
            {
                // If there is space in the smooth path, add the new start position.
                if (nsmoothPath < maxSmoothPathSize)
                {
                    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], newStartPos);
                    ++nsmoothPath;
                }
                // Move the iterator position to the other side of the off-mesh link.
                dtVcopy(iterPos, newEndPos);

                // Adjust the height of the iterator position.
                m_navMeshQuery->getPolyHeight(polys[0], iterPos, &iterPos[1]);
                iterPos[1] += 0.5f;
            }
        }

        // Store the current iterator position in the smooth path if there is space.
        if (nsmoothPath < maxSmoothPathSize)
        {
            dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
            ++nsmoothPath;
        }
    }

    *smoothPathSize = nsmoothPath;

    // Return success if the smooth path size is within the maximum limit.
    return nsmoothPath < Path::MAX_POINTS ? DT_SUCCESS : DT_FAILURE;
}

/**
 * @brief Checks whether two YZX points are within horizontal range and vertical tolerance.
 *
 * @param v1 The first point.
 * @param v2 The second point.
 * @param r The maximum horizontal range.
 * @param h The maximum vertical difference.
 * @return true if the points are within range; otherwise false.
 */
bool PathFinder::inRangeYZX(const float* v1, const float* v2, float r, float h) const
{
    const float dx = v2[0] - v1[0];
    const float dy = v2[1] - v1[1]; // elevation
    const float dz = v2[2] - v1[2];
    return (dx * dx + dz * dz) < r * r && fabsf(dy) < h;
}

/**
 * @brief Checks whether two path points are within horizontal range and vertical tolerance.
 *
 * @param p1 The first point.
 * @param p2 The second point.
 * @param r The maximum horizontal range.
 * @param h The maximum vertical difference.
 * @return true if the points are within range; otherwise false.
 */
bool PathFinder::inRange(const Vector3& p1, const Vector3& p2, float r, float h) const
{
    Vector3 d = p1 - p2;
    return (d.x * d.x + d.y * d.y) < r * r && fabsf(d.z) < h;
}

/**
 * @brief Returns the squared three-dimensional distance between two points.
 *
 * @param p1 The first point.
 * @param p2 The second point.
 * @return float The squared distance.
 */
float PathFinder::dist3DSqr(const Vector3& p1, const Vector3& p2) const
{
    return (p1 - p2).squaredLength();
}
