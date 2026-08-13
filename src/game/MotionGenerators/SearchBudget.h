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

#ifndef MANGOS_SEARCHBUDGET_H
#define MANGOS_SEARCHBUDGET_H

#include "Platform/Define.h"

#include <algorithm>

/**
 * @brief How the routed path is turned into points, and how many of them there may be.
 *
 * These were four preprocessor defines in the router's header, so anything that
 * included it to construct a PathFinder also acquired the names SMOOTH_PATH_SLOP and
 * MAX_POINT_PATH_LENGTH, and could neither scope them nor see where they came from.
 * They are typed constants in a namespace now; nothing else about them changes.
 */
namespace Nav
{
    /**
     * @brief Points a smoothed path may contain.
     *
     * Was 74, on the reasoning that 74 * 4 yards is 296 and that is far past evade
     * range. Two measurements say otherwise.
     *
     * The client accepts more: across four retail captures the longest monster-move
     * carried 93 points, so 74 was never a limit the protocol imposed -- it was one
     * this server chose, and then forgot it had chosen.
     *
     * And it bites. On the live server, eleven smoothed paths in one session stopped
     * because they ran out of points, several of them still ninety yards or more from
     * their goal. Those are not creatures wandering too far; they are ordinary routes
     * through geometry that needs more corners than the budget allowed.
     *
     * 93 is what retail was seen to send. Going past it would be inventing headroom
     * nobody has observed the client using.
     */
    constexpr uint32 MAX_POINTS = 93;

    /// Distance the smoother advances along the surface per step, in yards.
    constexpr float SMOOTH_STEP = 4.0f;

    /// How close to a steer target counts as having reached it, in yards.
    constexpr float SMOOTH_SLOP = 0.3f;

    /// How far above the surface the smoother samples, in yards.
    constexpr float SMOOTH_HEIGHT = 1.0f;

    /// The default ceiling on a routed path, in yards: the whole point budget spent.
    constexpr float DEFAULT_LENGTH = float(MAX_POINTS) * SMOOTH_STEP;

    /**
     * @brief What one routing request may spend.
     *
     * A value passed to the request, where it used to be a setter that mutated the router
     * and stayed mutated. That mattered as soon as a router outlived a single leg: the
     * limit was STICKY, so an unlimited request issued after a capped one -- a chase after
     * a flee -- silently inherited the cap, and the only defence was for every caller to
     * remember to re-apply a default it should never have had to know about.
     *
     * Only the point count lives here. The polygon bound belongs to Corridor, which owns
     * the storage it bounds, and the node pool is fixed when the Detour query is created
     * rather than per request -- see MMAP::MMAP_QUERY_MAX_NODES. Naming this one a budget
     * is what keeps them from being confused again: how far a creature is ALLOWED to chase
     * is a rule of the game, expressed in yards, and how many points a path may hold is a
     * property of the buffer that holds it.
     */
    struct SearchBudget
    {
        /// Points the produced path may contain, never above Nav::MAX_POINTS.
        uint32 points = Nav::MAX_POINTS;

        /**
         * @brief A budget for a route of at most @p yards.
         *
         * A non-positive length means "no rule of the game applies here", which is the full
         * budget rather than an empty one -- the caller is declining to cap the route, not
         * asking for a path of no points.
         */
        static SearchBudget ForLength(float yards)
        {
            SearchBudget budget;
            if (yards > 0.0f)
            {
                budget.points = std::min<uint32>(uint32(yards / Nav::SMOOTH_STEP),
                                                 Nav::MAX_POINTS);
            }
            return budget;
        }
    };
}

#endif // MANGOS_SEARCHBUDGET_H
