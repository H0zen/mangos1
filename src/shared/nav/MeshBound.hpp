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

#pragma once

/**
 * @file MeshBound.hpp
 * @brief RULING AREAS OUT BEFORE THE SEARCH REACHES THEM.
 *
 * After Koch & Funke, "Guiding the Search for the Euclidean Shortest Path Problem"
 * (SoCS 2025). The idea is one line: if the shortest route that touches area M is
 * already longer than a route we HAVE, then M is not on the shortest route, and the
 * search never needs to look at it.
 *
 *     lower(s, M) + lower(M, t) > upper(s, t)   =>   M cannot be on the optimal path
 *
 * Both sides have to be honest for this to keep the answer optimal, and they are: the
 * left is a true lower bound on any route through M, the right is the length of a real
 * route. An area is discarded only when no path through it could possibly win.
 *
 * The paper's motivating case is exactly ours. Polyanya's own heuristic is the straight
 * line to the target, and a straight line is a bad guide wherever the geometry doubles
 * back -- their example is a horseshoe, ours is a bay: the search pours into the water
 * because the target is across it, and finds out only after expanding the whole inlet.
 * Their measurements say the straight-line heuristic "starts to struggle as the problem
 * size grows", which is the same thing our own numbers said before the dominance check
 * went in.
 *
 * == Where this departs from the paper, deliberately ==
 *
 * They evaluate `min over the corners of the triangle` of `|s-c| + |c-t|`, on the
 * reasoning that a path crossing a triangle it does not contain must round a corner.
 * That is not true of the quantity being minimised. `f(p) = |s-p| + |p-t|` is CONVEX, so
 * over a convex area its maximum is at a corner and its minimum need not be: on an edge
 * it generally falls strictly between the two ends. Taking corners therefore returns
 * something too LARGE for a lower bound, and a bound that is too large discards areas
 * that were allowed -- which costs optimality, quietly, on exactly the long queries the
 * method exists for.
 *
 * So the minimum here is exact. Over an axis-aligned box it costs four reflections and
 * closes the gap for free.
 *
 * == What is not implemented, and why ==
 *
 * The paper's stronger "shrunken instance" bound routes on a simplified copy of the
 * world with its convex corners shaved off, and reads exact distances out of a
 * visibility graph over it, accelerated by contraction hierarchies and hub labelling.
 * Their preprocessing for that is twenty-four hours over the planet's coastlines. Ours
 * is a tile that may be re-baked between two runs of the server, so the Euclidean form
 * -- which the paper reports needs no preprocessing beyond the mesh itself -- is the
 * one that fits. If a tile ever gains a durable index, this is the interface it plugs
 * into.
 */

namespace Nav
{
    /**
     * @brief The shortest possible route from `s` to `t` that touches an axis-aligned
     *        box, ignoring every obstacle.
     *
     * `min over p in the box of (|s - p| + |p - t|)`, computed exactly.
     *
     * Ignoring obstacles is what makes it a LOWER bound and therefore safe: a real route
     * through the box is bent by whatever is in the way and can only be longer. When the
     * straight segment from `s` to `t` already crosses the box the answer is `|s - t|`,
     * which rules nothing out -- correctly, since the box is sitting on the direct line.
     *
     * Coordinates are plan coordinates, in yards. Height is deliberately absent: the
     * mesh's own lengths are measured in plan, so a bound that included Z would be
     * comparing against a different quantity and could exceed it.
     */
    float BoxDetourLength(float sx, float sy, float tx, float ty,
                          float minX, float minY, float maxX, float maxY);

    /**
     * @brief Can any route from `s` to `t` through this box beat `upper`?
     *
     * @param upper a length that is genuinely achievable; anything shorter and areas
     *              that belong on the optimal path start being thrown away. Pass a
     *              non-positive value to disable the test, which is what a caller with
     *              no route in hand must do.
     * @return false only when the box is provably useless.
     */
    bool BoxCanCarryPath(float sx, float sy, float tx, float ty,
                         float minX, float minY, float maxX, float maxY,
                         float upper);
}
