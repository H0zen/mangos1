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

#ifndef MANGOSSERVER_MOVESPLINEINIT_H
#define MANGOSSERVER_MOVESPLINEINIT_H

/**
 * @file MoveSplineInit.h
 * @brief LAUNCHING A LEG. One engine now, and it is the plan.
 *
 * == What used to be here ==
 *
 * A second movement engine: `MoveSpline` with its own copy of the geometry, its own
 * Catmull-Rom evaluator, its own length table, its own clock and its own packet builder.
 * The server's idea of where a unit stood came from it, while what the client had been
 * told came from a `Course` -- two answers to one question, agreeing until they did not.
 * Every disagreement was a unit standing somewhere nobody was drawing it: an arrival
 * announced before the client got there, a pose 2.8 yards behind at a run, a leg the
 * create block described differently from the monster-move that launched it.
 *
 * All of it is deleted. `Helm::Course` is the plan, the wire and the position, and this
 * class is what turns a caller's request into one.
 *
 * == Why the name stayed ==
 *
 * Roughly a hundred and eighty call sites say `MoveSplineInit init(*unit); init.MoveTo(…);
 * init.Launch();` and every one of them means "send this unit there". Renaming the type
 * would have been a hundred and eighty edits that change nothing and hide, in the noise,
 * the handful that change something. It is worth renaming; it is not worth doing in the
 * same commit that removes an engine.
 */

#include "MotionGenerators/Pathing.h"
#include "motion/Course.h"

// For UnitMoveType, which this header DECLARES a function returning. It used to arrive
// through the router's header; that header no longer drags the whole Unit in, so the
// dependency has to be spelled where it is used. Exactly the case the coding standard
// means by "include what you use": it compiled only because something four levels up
// happened to bring it.
#include "Object/Unit.h"

#include <vector>

namespace Movement
{
    using Motion::PointsArray;
    using Motion::Vector3;

    /**
     * @brief Which of the unit's speeds a leg with these movement flags travels at.
     *
     * Declared here because it is not only the launcher's business any more: anything
     * that has to turn a duration back into yards -- how far a lagging client is behind,
     * for one -- needs the same answer the launcher used, and deriving it a second time
     * is how the two drift apart.
     */
    UnitMoveType SelectSpeedType(uint32 moveFlags);

    /**
     * @brief Builds one leg and sends it.
     *
     * Accumulate what the leg is (`MoveTo`, `MovebyPath`, `SetWalk`, `SetFacing`, …),
     * then `Launch()`. What comes back is the duration the CLIENT was told, which is the
     * only duration worth waiting on.
     */
    class MoveSplineInit
    {
        public:

            /**
             * @brief Constructor that initializes the MoveSplineInit with a reference to a Unit.
             * @param m Reference to the Unit to be moved.
             */
            explicit MoveSplineInit(Unit& m);

            /**
             * @brief Build the leg, send it, and make it the unit's course.
             * @return int32 duration in milliseconds, or 0 when nothing was sent.
             */
            int32 Launch();

            /**
             * @brief Stops any creature movement.
             */
            void Stop(bool forceSend = false);

            /**
             * @brief Adds final facing animation.
             * Sets unit's facing to specified point/angle after all path done.
             * You can have only one final facing: previous will be overridden.
             * @param angle The angle to face.
             */
            void SetFacing(float angle);

            /**
             * @brief Sets unit's facing to a specified point after all path done.
             * @param point The point to face.
             */
            void SetFacing(Vector3 const& point);

            /**
             * @brief Sets unit's facing to a specified target after all path done.
             * @param target The target to face. A null target leaves the facing alone
             *        rather than dereferencing it -- several callers pass whatever a
             *        lookup returned.
             */
            void SetFacing(const Unit* target);

            /**
             * @brief Initializes movement by path.
             * @param path Array of points, shouldn't be empty.
             * @param pointId Id of first point of the path. Example: when the third path point is done, it will notify that pointId + 3 is done.
             */
            void MovebyPath(const PointsArray& path, int32 pointId = 0);

            /**
             * @brief Initializes simple A to B motion, A is the current unit's position, B is the destination.
             * @param destination The destination point.
             * @param generatePath Whether to generate a path.
             * @param forceDestination Whether to force the destination.
             * @param maxPathRange The maximum path range.
             */
            void MoveTo(const Vector3& destination, bool generatePath = false, bool forceDestination = false, float maxPathRange = 0.0f);

            /**
             * @brief Initializes simple A to B motion, A is the current unit's position, B is the destination.
             * @param x The x-coordinate of the destination.
             * @param y The y-coordinate of the destination.
             * @param z The z-coordinate of the destination.
             * @param generatePath Whether to generate a path.
             * @param forceDestination Whether to force the destination.
             * @param maxPathRange The maximum path range.
             */
            void MoveTo(float x, float y, float z, bool generatePath = false, bool forceDestination = false, float maxPathRange = 0.0f);

            /**
             * @brief Sets the Id of the first point of the path.
             * When the N-th path point is done, the listener will notify that pointId + N is done.
             * Needed for waypoint movement where the path is split into parts.
             * @param pointId The Id of the first point of the path.
             */
            void SetFirstPointId(int32 pointId) { m_pointIdOffset = pointId; }

            /**
             * @brief Enables CatmullRom interpolation, which in this wire format IS the
             *        flying animation -- one bit selects both.
             */
            void SetFly() { m_flying = true; }

            /**
             * @brief Enables walk mode. Disabled by default.
             * @param enable Whether to enable walk mode.
             */
            void SetWalk(bool enable) { m_walking = enable; }

            /**
             * @brief Enables falling mode. Disabled by default.
             *
             * A fall is timed by gravity, not by a speed -- see `Helm::Course::Falling`.
             */
            void SetFall() { m_falling = true; }

            /**
             * @brief Sets the velocity (in case you want to have custom movement velocity).
             * If not set, speed will be selected based on the unit's speeds and current movement mode.
             * Has no effect if falling mode is enabled.
             * @param velocity The velocity, shouldn't be negative.
             */
            void SetVelocity(float velocity) { m_velocity = velocity; }

            /**
             * @brief Gets the path points array.
             * @return PointsArray The path points array.
             */
            PointsArray& Path() { return m_path; }

        protected:

            Unit&        unit;               /**< The unit to be moved. */
            PointsArray  m_path;             /**< Where it goes, first point included. */
            Helm::Facing m_facing;           /**< What it faces when it gets there. */
            float        m_velocity = 0.0f;  /**< Zero means "read it off the unit". */
            int32        m_pointIdOffset = 0;
            bool         m_walking = false;
            bool         m_flying = false;
            bool         m_falling = false;
    };

    /**
     * @brief Initializes movement by path.
     * @param controls Array of points, shouldn't be empty.
     * @param path_offset Id of the first point of the path.
     */
    inline void MoveSplineInit::MovebyPath(const PointsArray& controls, int32 path_offset)
    {
        m_pointIdOffset = path_offset;
        m_path.assign(controls.begin(), controls.end());
    }

    /**
     * @brief Initializes simple A to B motion, A is the current unit's position, B is the destination.
     */
    inline void MoveSplineInit::MoveTo(float x, float y, float z, bool generatePath, bool forceDestination, float maxPathRange)
    {
        Vector3 v(x, y, z);
        MoveTo(v, generatePath, forceDestination, maxPathRange);
    }

    /**
     * @brief Initializes simple A to B motion, A is the current unit's position, B is the destination.
     */
    inline void MoveSplineInit::MoveTo(const Vector3& dest, bool generatePath, bool forceDestination, float maxPathRange)
    {
        if (generatePath)
        {
            Pathing path(&unit);
            path.calculate(dest.x, dest.y, dest.z, forceDestination,
                           Nav::SearchBudget::ForLength(maxPathRange));
            MovebyPath(path.getPath());
        }
        else
        {
            m_pointIdOffset = 0;
            m_path.resize(2);
            m_path[1] = dest;
        }
    }

    /**
     * @brief Sets unit's facing to a specified point after all path done.
     * @param spot The point to face.
     */
    inline void MoveSplineInit::SetFacing(Vector3 const& spot)
    {
        m_facing = Helm::Facing::ToSpot(Helm::Vector3(spot.x, spot.y, spot.z));
    }
}

#endif // MANGOSSERVER_MOVESPLINEINIT_H
