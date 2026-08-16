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

#include "nav/Polyanya.hpp"

#include "nav/MeshBound.hpp"

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace Nav
{
    namespace
    {
        constexpr float EPS = 1e-4f;

        struct Vec2
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; }
        Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; }
        Vec2 operator*(const Vec2& a, float s) { return Vec2{a.x * s, a.y * s}; }

        float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
        float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
        float Length(const Vec2& a) { return std::sqrt(Dot(a, a)); }
        float Distance(const Vec2& a, const Vec2& b) { return Length(a - b); }

        Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }

        /**
         * @brief One state of the search: an interval, seen from a root.
         *
         * `g` is the distance to the ROOT, not to the interval. Everything past the root
         * is still undecided -- that is the point of carrying an interval rather than a
         * point, and why no smoothing pass is needed afterwards.
         */
        struct Node
        {
            uint32_t portal = 0;
            float lo = 0.0f;        ///< parameter along the portal segment
            float hi = 1.0f;
            bool loCorner = false;  ///< may the path bend at this end?
            bool hiCorner = false;
            Vec2 root;
            float g = 0.0f;
            float f = 0.0f;
            int32_t parent = -1;
        };

        struct ByF
        {
            const std::vector<Node>* nodes;
            bool operator()(uint32_t a, uint32_t b) const
            {
                return (*nodes)[a].f > (*nodes)[b].f;
            }
        };

        /**
         * @brief A lower bound on the distance from a root to the target via an interval.
         *
         * Reflected, which is what makes it tight rather than merely admissible: when the
         * root and the target are on the same side of the interval's line, the shortest
         * route through the interval is the straight line to the target's MIRROR image.
         * If that line crosses the interval the bound is exact; otherwise the path must
         * pass an endpoint, and the bound is the shorter of the two ways round.
         */
        float Heuristic(const Vec2& root, const Vec2& a, const Vec2& b, const Vec2& goal)
        {
            const Vec2 edge = b - a;
            const float len2 = Dot(edge, edge);
            if (len2 < EPS)
            {
                return Distance(root, a) + Distance(a, goal);
            }

            const float sideRoot = Cross(edge, root - a);
            const float sideGoal = Cross(edge, goal - a);

            Vec2 target = goal;
            if (sideRoot * sideGoal > 0.0f)
            {
                // Reflect the goal across the interval's line.
                const float t = Dot(goal - a, edge) / len2;
                const Vec2 foot = a + edge * t;
                target = foot + (foot - goal);
            }

            // Does root -> target cross the interval itself?
            const Vec2 d = target - root;
            const float denom = Cross(d, edge);
            if (std::fabs(denom) > EPS)
            {
                const float s = Cross(a - root, edge) / denom;
                const float u = Cross(a - root, d) / -denom;
                if (s >= 0.0f && u >= -EPS && u <= 1.0f + EPS)
                {
                    return Distance(root, target);
                }
            }

            return std::min(Distance(root, a) + Distance(a, goal),
                            Distance(root, b) + Distance(b, goal));
        }

        /// Where the ray root->through meets the line of segment (c, d), as a parameter
        /// along that segment. Returns false when they are parallel.
        bool RayParameter(const Vec2& root, const Vec2& through, const Vec2& c,
                          const Vec2& d, float& out)
        {
            const Vec2 dir = through - root;
            const Vec2 edge = d - c;
            const float denom = Cross(dir, edge);
            if (std::fabs(denom) < EPS)
            {
                return false;
            }

            out = Cross(root - c, dir) / -denom;
            return true;
        }

        /// Is `p` strictly inside the wedge that opens from `root` through [a, b]?
        /// Points behind the root are outside it, which is what keeps a search from
        /// turning back through the opening it just came from.
        bool InsideWedge(const Vec2& root, const Vec2& a, const Vec2& b, const Vec2& p)
        {
            const Vec2 ra = a - root;
            const Vec2 rb = b - root;
            const Vec2 rp = p - root;

            if (Dot(rp, ra + rb) <= 0.0f)
            {
                return false;
            }

            const float left = Cross(ra, rp);
            const float right = Cross(rb, rp);
            return left * right <= EPS;
        }
    }

    MeshPath FindMeshPath(const NavTile& tile, const TileMesh& mesh,
                          const MeshQuery& query)
    {
        MeshPath result;

        const Vec2 start{query.startX, query.startY};
        const Vec2 goal{query.endX, query.endY};

        // BY HEIGHT, not by plan. With stacked floors a square holds several
        // rectangles, and taking the first is taking the lowest -- which starts the
        // search on the floor under the one the query meant.
        const int32_t startRect =
            RectAtHeight(tile, mesh, start.x, start.y, query.startZ);
        const int32_t goalRect = RectAtHeight(tile, mesh, goal.x, goal.y, query.endZ);
        if (startRect < 0 || goalRect < 0)
        {
            return result;
        }

        const uint8_t needed = QuantiseClearance(query.profile.radius);

        // Width AND permission, in the one place every step of the search asks. The width
        // figure is the rectangle's own, which is an upper bound and can only ever say
        // "maybe" -- the portal's narrowest is the real filter and is tested beside it.
        // The area is not a bound at all: a rectangle covers cells of exactly one area,
        // so a mover this refuses could not stand anywhere in it.
        // The rectangle's own footprint in world plan. Cell indices grow as world
        // coordinates fall, so the low index is the HIGH coordinate, and the outer face
        // of a border row is half a cell beyond its centre.
        const auto boxOf = [&](uint32_t rect, float& minX, float& minY,
                               float& maxX, float& maxY)
        {
            const NavRect& r = mesh.rects[rect];
            maxX = CellCentre(GlobalCell(tile.TileX(), r.x0)) + CELL_SIZE * 0.5f;
            minX = CellCentre(GlobalCell(tile.TileX(), r.x1)) - CELL_SIZE * 0.5f;
            maxY = CellCentre(GlobalCell(tile.TileY(), r.y0)) + CELL_SIZE * 0.5f;
            minY = CellCentre(GlobalCell(tile.TileY(), r.y1)) - CELL_SIZE * 0.5f;
        };

        // Width AND permission, in the one place every step of the search asks. The width
        // figure is the rectangle's own, which is an upper bound and can only ever say
        // "maybe" -- the portal's narrowest is the real filter and is tested beside it.
        // The area is not a bound at all: a rectangle covers cells of exactly one area,
        // so a mover this refuses could not stand anywhere in it.
        //
        // The third test is the caller's route, if it has one: an area whose cheapest
        // conceivable detour already loses to a route we hold cannot be on the shortest
        // one. It is last because it is the only one that costs square roots, and the
        // two ahead of it reject most of what it would.
        const auto passable = [&](uint32_t rect)
        {
            if (mesh.rects[rect].clearance < needed ||
                !query.profile.AdmitsGround(mesh.rects[rect].area))
            {
                return false;
            }

            if (!(query.upperBound > 0.0f))
            {
                return true;
            }

            float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
            boxOf(rect, minX, minY, maxX, maxY);
            return BoxCanCarryPath(start.x, start.y, goal.x, goal.y,
                                   minX, minY, maxX, maxY, query.upperBound);
        };

        if (!passable(static_cast<uint32_t>(startRect)) ||
            !passable(static_cast<uint32_t>(goalRect)))
        {
            return result;
        }

        // Nothing to search: one convex area, so the straight line is the answer and is
        // inside it by definition.
        if (startRect == goalRect)
        {
            result.found = true;
            result.length = Distance(start, goal);
            result.points.push_back(Geometry::Vector3(start.x, start.y, 0.0f));
            result.points.push_back(Geometry::Vector3(goal.x, goal.y, 0.0f));
            return result;
        }

        std::vector<Node> nodes;
        nodes.reserve(1024);

        ByF order{&nodes};
        std::priority_queue<uint32_t, std::vector<uint32_t>, ByF> open(order);

        // The segment of a portal, cached as it is asked for repeatedly.
        const auto segmentOf = [&](uint32_t portal, Vec2& a, Vec2& b)
        {
            float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
            PortalSegment(tile, mesh, mesh.portals[portal], ax, ay, bx, by);
            a = Vec2{ax, ay};
            b = Vec2{bx, by};
        };

        /**
         * THE SEARCH HAS TO BE ABLE TO STOP, and nothing above makes it.
         *
         * A child seen straight through an opening keeps its parent's root and its
         * parent's `g` -- correctly, no distance is walked by looking further through
         * the same wedge. So a root that can see many openings hands out any number of
         * children costing exactly the same, and the only gate before this was
         * `f < best`, where `best` is infinity until the goal has been seen at least
         * once. Nothing else recorded that a portal had already been reached.
         *
         * The result is a search that does not terminate. On a real coastal tile --
         * 7,846 rectangles, 36,247 openings -- one thirty-yard query still hit the
         * expansion cap at FOUR MILLION expansions, in both directions: forwards it
         * kept the first path it stumbled on (119 yards for a 30-yard walk, shaped
         * like a hairpin) and backwards it never saw the goal at all, so nothing ever
         * pruned anything. Every creature chasing anything paid that, every replan.
         *
         * So: the cheapest `g` any node has reached each opening with. A child that
         * arrives no more cheaply than one already did has nothing new to offer and is
         * dropped. `g` never decreases along a path and there are finitely many
         * openings, so the search now has a bound that does not depend on a cap.
         *
         * BUCKETED BY WHERE ON THE OPENING, because an interval is not a point: two
         * genuinely different crossings of one wide doorway lead to different corners
         * to bend around, and collapsing them to a single number would prune the
         * second before it was tried. Eight is coarse enough to be cheap and fine
         * enough that the buckets are wider than the corners are apart.
         */
        enum : uint32_t { INTERVAL_BUCKETS = 8 };

        std::vector<float> reached(mesh.portals.size() * INTERVAL_BUCKETS,
                                   std::numeric_limits<float>::max());

        const auto worthPushing = [&](const Node& node)
        {
            const float middle = (node.lo + node.hi) * 0.5f;
            uint32_t bucket = static_cast<uint32_t>(middle * float(INTERVAL_BUCKETS));
            if (bucket >= INTERVAL_BUCKETS)
            {
                bucket = INTERVAL_BUCKETS - 1;
            }

            const size_t slot =
                static_cast<size_t>(node.portal) * INTERVAL_BUCKETS + bucket;
            if (node.g >= reached[slot] - EPS)
            {
                return false;
            }

            reached[slot] = node.g;
            return true;
        };

        const auto push = [&](const Node& node)
        {
            if (!worthPushing(node))
            {
                return;
            }

            nodes.push_back(node);
            open.push(static_cast<uint32_t>(nodes.size() - 1));
        };

        // The root of the search: every opening out of the start's own rectangle, whole,
        // seen from the start point.
        for (uint32_t i = mesh.first[startRect]; i < mesh.first[startRect + 1]; ++i)
        {
            // A rim portal leaves the tile, and this search answers within one. Skipping
            // it is not a limitation of the search -- what lies beyond belongs to another
            // file, and crossing is the store's business. Reading it as a neighbour index
            // would be reading 0xFFFFFFFF as a rectangle.
            // The PORTAL's width, not just the rectangle's. A rectangle reports the
            // widest room it offers anywhere -- it has to, since a maximal one always
            // touches the rim -- so it can only ever say "maybe". The opening is where
            // a mover actually has to fit, and its figure is the narrowest along the
            // run, which is exactly what a doorway is.
            if (mesh.portals[i].LeavesTheTile() ||
                mesh.portals[i].clearance < needed ||
                !passable(mesh.portals[i].neighbour))
            {
                continue;
            }

            Vec2 a, b;
            segmentOf(i, a, b);

            Node node;
            node.portal = i;
            node.lo = 0.0f;
            node.hi = 1.0f;
            node.loCorner = true;
            node.hiCorner = true;
            node.root = start;
            node.g = 0.0f;
            node.f = Heuristic(start, a, b, goal);
            push(node);
        }

        int32_t bestNode = -1;
        Vec2 bestRoot;
        float best = std::numeric_limits<float>::max();

        while (!open.empty())
        {
            if (result.expansions >= query.maxExpansions)
            {
                // Out of budget rather than out of frontier. Said out loud, because
                // what is in hand at this moment is not the shortest path and the
                // caller has to be able to tell.
                result.exhausted = true;
                break;
            }

            const uint32_t current = open.top();
            open.pop();

            if (nodes[current].f >= best - EPS)
            {
                break;      // nothing left can beat what is already in hand
            }

            ++result.expansions;

            const Node node = nodes[current];
            const Portal& portal = mesh.portals[node.portal];
            if (portal.LeavesTheTile())
            {
                continue;   // nothing on this side of the file to step into
            }

            const uint32_t poly = portal.neighbour;

            Vec2 pa, pb;
            segmentOf(node.portal, pa, pb);
            const Vec2 a = Lerp(pa, pb, node.lo);
            const Vec2 b = Lerp(pa, pb, node.hi);

            // Is the goal in the polygon we are stepping into? Then this node either
            // finishes the search or bends once more to do it.
            if (poly == static_cast<uint32_t>(goalRect))
            {
                float total = std::numeric_limits<float>::max();
                Vec2 via = node.root;

                if (InsideWedge(node.root, a, b, goal))
                {
                    total = node.g + Distance(node.root, goal);
                }
                else
                {
                    if (node.loCorner)
                    {
                        const float cost =
                            node.g + Distance(node.root, a) + Distance(a, goal);
                        if (cost < total)
                        {
                            total = cost;
                            via = a;
                        }
                    }
                    if (node.hiCorner)
                    {
                        const float cost =
                            node.g + Distance(node.root, b) + Distance(b, goal);
                        if (cost < total)
                        {
                            total = cost;
                            via = b;
                        }
                    }
                }

                if (total < best)
                {
                    best = total;
                    bestNode = static_cast<int32_t>(current);
                    bestRoot = via;
                }
            }

            // Project the wedge onto every other opening of that polygon.
            for (uint32_t i = mesh.first[poly]; i < mesh.first[poly + 1]; ++i)
            {
                const Portal& next = mesh.portals[i];
                if (next.LeavesTheTile() || next.neighbour == portal.rect ||
                    next.clearance < needed || !passable(next.neighbour))
                {
                    continue;   // never out of the tile, never back, never too narrow
                }

                Vec2 c, d;
                segmentOf(i, c, d);

                // The wedge's two rays cut the target segment at these parameters; with
                // 0 and 1 they are the boundaries of every piece worth considering.
                float cuts[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                int count = 2;

                float t = 0.0f;
                if (RayParameter(node.root, a, c, d, t) && t > 0.0f && t < 1.0f)
                {
                    cuts[count++] = t;
                }
                if (RayParameter(node.root, b, c, d, t) && t > 0.0f && t < 1.0f)
                {
                    cuts[count++] = t;
                }
                std::sort(cuts, cuts + count);

                for (int piece = 0; piece + 1 < count; ++piece)
                {
                    const float lo = cuts[piece];
                    const float hi = cuts[piece + 1];
                    if (hi - lo < EPS)
                    {
                        continue;
                    }

                    const Vec2 middle = Lerp(c, d, (lo + hi) * 0.5f);

                    Node child;
                    child.portal = i;
                    child.lo = lo;
                    child.hi = hi;
                    child.parent = static_cast<int32_t>(current);

                    if (InsideWedge(node.root, a, b, middle))
                    {
                        // Seen directly. The root does not move, so nothing about the
                        // path before this point is decided yet.
                        child.root = node.root;
                        child.g = node.g;
                    }
                    else
                    {
                        // Outside the wedge: the path has to bend round one end of the
                        // interval, and that end becomes the new root. Which end is
                        // whichever side of the wedge the piece lies on. A bend is only
                        // offered where the interval end is a corner of the mesh -- in
                        // the middle of an opening there is nothing to bend around.
                        const bool pastA =
                            Cross(a - node.root, middle - node.root) *
                                Cross(a - node.root, b - node.root) < 0.0f;

                        if (pastA)
                        {
                            if (!node.loCorner)
                            {
                                continue;
                            }
                            child.root = a;
                            child.g = node.g + Distance(node.root, a);
                        }
                        else
                        {
                            if (!node.hiCorner)
                            {
                                continue;
                            }
                            child.root = b;
                            child.g = node.g + Distance(node.root, b);
                        }
                    }

                    // An end of the new interval is a corner exactly when it is an end
                    // of the opening itself: past it the boundary is a wall, or another
                    // opening into somewhere else.
                    child.loCorner = lo <= EPS;
                    child.hiCorner = hi >= 1.0f - EPS;

                    const Vec2 lowPoint = Lerp(c, d, lo);
                    const Vec2 highPoint = Lerp(c, d, hi);
                    child.f = child.g + Heuristic(child.root, lowPoint, highPoint, goal);

                    if (child.f < best - EPS)
                    {
                        push(child);
                    }
                }
            }
        }

        if (bestNode < 0)
        {
            return result;
        }

        // Walk the roots back. They ARE the turning points: the root only ever moved
        // where the path had to bend, so what comes out is already taut.
        std::vector<Vec2> reversed;
        reversed.push_back(goal);
        if (Distance(bestRoot, goal) > EPS)
        {
            reversed.push_back(bestRoot);
        }

        for (int32_t at = bestNode; at >= 0; at = nodes[static_cast<size_t>(at)].parent)
        {
            const Vec2& root = nodes[static_cast<size_t>(at)].root;
            if (Distance(root, reversed.back()) > EPS)
            {
                reversed.push_back(root);
            }
        }

        if (Distance(reversed.back(), start) > EPS)
        {
            reversed.push_back(start);
        }

        result.found = true;
        result.length = 0.0f;
        for (size_t i = reversed.size(); i-- > 0;)
        {
            result.points.push_back(
                Geometry::Vector3(reversed[i].x, reversed[i].y, 0.0f));
            if (result.points.size() > 1)
            {
                const size_t n = result.points.size();
                const Geometry::Vector3& p = result.points[n - 2];
                const Geometry::Vector3& q = result.points[n - 1];
                result.length += std::sqrt((q.x - p.x) * (q.x - p.x) +
                                           (q.y - p.y) * (q.y - p.y));
            }
        }

        return result;
    }
}
