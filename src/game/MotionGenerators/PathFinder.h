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

#ifndef MANGOS_PATH_FINDER_H
#define MANGOS_PATH_FINDER_H

#include <algorithm>
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include "Corridor.h"
#include "MoveMapSharedDefines.h"
#include "MoveProfile.h"
#include "Route.h"
#include "SearchBudget.h"
#include "movement/MoveSplineInitArgs.h"

using Movement::Vector3;
using Movement::PointsArray;

class Unit;

/// Floats per point in Detour's arrays. Its own axis order, not the world's.
constexpr int VERTEX_SIZE = 3;

/// The polygon reference that refers to nothing.
constexpr dtPolyRef INVALID_POLYREF = 0;

/**
 * @brief Class responsible for finding paths for units.
 */
class PathFinder
{
    public:
        /**
         * @brief Constructor for PathFinder.
         * @param owner Pointer to the unit that owns this PathFinder.
         */
        PathFinder(Unit const* owner);

        /// Route on a map the mover is not filed under. A vessel's deck is its own map,
        /// and a boarded unit walks that navmesh while the world still holds its guid.
        PathFinder(Unit const* owner, uint32 mapId);

        /**
         * @brief Destructor for PathFinder.
         */
        ~PathFinder();

        /**
         * @brief Calculate the path from owner to given destination.
         * @param destX X-coordinate of the destination.
         * @param destY Y-coordinate of the destination.
         * @param destZ Z-coordinate of the destination.
         * @param forceDest Whether to force the destination.
         * @return True if a new path was calculated, false otherwise (no change needed).
         */
        bool calculate(float destX, float destY, float destZ, bool forceDest = false,
                       Nav::SearchBudget budget = Nav::SearchBudget());

        /**
         * @brief Calculate the path from an explicit start position to given destination.
         * @param startX X-coordinate of the start position.
         * @param startY Y-coordinate of the start position.
         * @param startZ Z-coordinate of the start position.
         * @param destX X-coordinate of the destination.
         * @param destY Y-coordinate of the destination.
         * @param destZ Z-coordinate of the destination.
         * @param forceDest Whether to force the destination.
         * @return True if a new path was calculated, false otherwise (no change needed).
         */
        bool calculate(float startX, float startY, float startZ, float destX, float destY, float destZ, bool forceDest = false,
                       Nav::SearchBudget budget = Nav::SearchBudget());

        // Option setters - use optional
        /**
         * @brief Set whether to use a straight path.
         * @param useStraightPath Whether to use a straight path.
         */
        void setUseStrightPath(bool useStraightPath) { m_useStraightPath = useStraightPath; };


        // Result getters
        /**
         * @brief Get the start position of the path.
         * @return The start position of the path.
         */
        Vector3 getStartPosition() const { return m_startPosition; }

        /**
         * @brief Get the end position of the path.
         * @return The end position of the path.
         */
        Vector3 getEndPosition() const { return m_endPosition; }

        /**
         * @brief Get the actual end position of the path.
         * @return The actual end position of the path.
         */
        Vector3 getActualEndPosition() const { return m_actualEndPosition; }

        /**
         * @brief Get the path points.
         * @return The path points.
         */
        PointsArray const& getPath() const { return m_route.points; }

        /**
         * @brief Get the whole result of the last calculate().
         * @return The route: its points, its outcome and why it stopped.
         */
        Nav::Route const& getRoute() const { return m_route; }

    private:

        Nav::Corridor       m_corridor;         // Polygons this mover is following

        Nav::Route          m_route;      // The answer to the last calculate()

        // What the mover may do, snapshotted at the top of every calculate(). The
        // router reads it and never asks the unit itself: that is the whole of the
        // separation between deciding and routing.
        Nav::MoveProfile    m_profile;

        bool           m_useStraightPath;  // Type of path that will be generated
        bool           m_forceDestination; // When set, we will always arrive at the given point

        // What THIS request may spend. Assigned from calculate()'s argument rather than
        // left over from a setter, so it cannot outlive the request that asked for it.
        Nav::SearchBudget   m_budget;

        // Set by noteSearchLimit() while the search runs, folded into the route at the
        // end. It cannot be written straight into m_route.stop: the stop is assigned
        // wholesale once the poly path is known, which would drop the budget that
        // explains it. Nav::RouteStop::Reached stands for "no budget was hit".
        Nav::RouteStop      m_budgetStop;

        Vector3        m_startPosition;    // {x, y, z} of current location
        Vector3        m_endPosition;      // {x, y, z} of the destination
        Vector3        m_actualEndPosition;// {x, y, z} of the closest possible point to the given destination

        const Unit* const       m_sourceUnit;       // The unit that is moving
        const dtNavMesh*        m_navMesh;          // The navigation mesh
        const dtNavMeshQuery*   m_navMeshQuery;     // The navigation mesh query used to find the path

        dtQueryFilter m_filter;                     // Use a single filter for all movements, update it when needed

        /**
         * @brief Set the start position of the path.
         * @param point The start position.
         */
        void setStartPosition(const Vector3 &point) { m_startPosition = point; }

        /**
         * @brief Set the end position of the path.
         * @param point The end position.
         */
        void setEndPosition(const Vector3 &point) { m_actualEndPosition = point; m_endPosition = point; }

        /**
         * @brief Set the actual end position of the path.
         * @param point The actual end position.
         */
        void setActualEndPosition(const Vector3 &point) { m_actualEndPosition = point; }

        /**
         * @brief Clear the path data.
         */
        void clear()
        {
            m_corridor.Clear();
            m_route.points.clear();
        }

        /**
         * @brief Check if two points are in range.
         * @param p1 The first point.
         * @param p2 The second point.
         * @param r The range.
         * @param h The height.
         * @return True if the points are in range, false otherwise.
         */
        bool inRange(const Vector3& p1, const Vector3& p2, float r, float h) const;

        /**
         * @brief Calculate the squared 3D distance between two points.
         * @param p1 The first point.
         * @param p2 The second point.
         * @return The squared 3D distance between the points.
         */
        float dist3DSqr(const Vector3& p1, const Vector3& p2) const;

        /**
         * @brief Check if two points are in range in the YZX plane.
         * @param v1 The first point.
         * @param v2 The second point.
         * @param r The range.
         * @param h The height.
         * @return True if the points are in range, false otherwise.
         */
        bool inRangeYZX(const float* v1, const float* v2, float r, float h) const;

        /**
         * @brief Get the path polygon by position.
         * @param polyPath The polygon path.
         * @param polyPathSize The size of the polygon path.
         * @param point The position point.
         * @param distance The distance to the polygon.
         * @return The path polygon reference.
         */
        dtPolyRef getPathPolyByPosition(const dtPolyRef* polyPath, uint32 polyPathSize, const float* point, float* distance = NULL) const;

        /**
         * @brief Get the polygon by location.
         * @param point The location point.
         * @param distance The distance to the polygon.
         * @return The polygon reference.
         */
        dtPolyRef getPolyByLocation(const float* point, float* distance) const;

        /**
         * @brief Check if a tile exists at the given position.
         * @param p The position.
         * @return True if the tile exists, false otherwise.
         */
        bool HaveTile(const Vector3& p) const;

        /**
         * @brief Build the polygon path.
         * @param startPos The start position.
         * @param endPos The end position.
         */
        void BuildPolyPath(const Vector3& startPos, const Vector3& endPos);

        /**
         * @brief Build the point path.
         * @param startPoint The start point.
         * @param endPoint The end point.
         */
        void BuildPointPath(const float* startPoint, const float* endPoint);

        /**
         * @brief Build a shortcut path.
         *
         * Lays the points and NOTHING else: the outcome belongs to the caller, because
         * the same straight line is a legitimate answer for a flier and a refusal for a
         * walker. Leaving it unset means a caller that forgets keeps the refusing
         * default calculate() starts from, which is the safe direction to forget in.
         */
        void BuildShortcut();

        /**
         * @brief Record that a Detour search stopped at a budget rather than at the world.
         *
         * Detour reports both budgets as DETAIL bits on a SUCCESS status, so
         * dtStatusFailed() is false and the caller sees a short path with no reason
         * attached. Which budget it was matters to whoever has to fix it -- the node
         * pool is a server setting, the polygon buffer is Nav::Corridor::CAPACITY -- so the two
         * are logged apart even though both land in Nav::RouteStop.
         *
         * @param status The status returned by the Detour query.
         * @param where Name of the call site, for the log line.
         */
        void noteSearchLimit(dtStatus status, const char* where);

        /**
         * @brief Try to replace the whole search with the straight segment.
         *
         * @param startPoly Polygon the start position sits on.
         * @param endPoly Polygon the end position sits on.
         * @param startPoint Start position, in Detour's axis order.
         * @param endPoint End position, in Detour's axis order.
         * @return True when the segment is walkable AND provably optimal, in which case
         *         the corridor describes it; false to run the full search.
         */
        bool BuildStraightShortcut(dtPolyRef startPoly, dtPolyRef endPoly,
                                   const float* startPoint, const float* endPoint);

        /**
         * @brief Push the current profile's permissions and the area costs into
         *        the Detour filter.
         *
         * A translation and nothing more -- WHICH areas the mover may occupy was
         * decided by Nav::ProfileOf before this router ever saw it.
         */
        void applyFilter();

        // Smooth path auxiliary functions
        /**
         * @brief Fix up the corridor path.
         * @param path The path.
         * @param npath The number of path points.
         * @param maxPath The maximum path length.
         * @param visited The visited polygons.
         * @param nvisited The number of visited polygons.
         * @return The fixed up path length.
         */
        uint32 fixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath,
                             const dtPolyRef* visited, uint32 nvisited);

        /**
         * @brief Get the steer target for the path.
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
        bool getSteerTarget(const float* startPos, const float* endPos, float minTargetDist,
                            const dtPolyRef* path, uint32 pathSize, float* steerPos,
                            unsigned char& steerPosFlag, dtPolyRef& steerPosRef);

        /**
         * @brief Find the smooth path.
         * @param startPos The start position.
         * @param endPos The end position.
         * @param polyPath The polygon path.
         * @param polyPathSize The size of the polygon path.
         * @param smoothPath The smooth path.
         * @param smoothPathSize The size of the smooth path.
         * @param smoothPathMaxSize The maximum size of the smooth path.
         * @return The status of the path finding.
         */
        dtStatus findSmoothPath(const float* startPos, const float* endPos,
                                const dtPolyRef* polyPath, uint32 polyPathSize,
                                float* smoothPath, int* smoothPathSize, uint32 smoothPathMaxSize);
};

#endif // MANGOS_PATH_FINDER_H
