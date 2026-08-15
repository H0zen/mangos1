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
 * @file NavMesh.hpp
 * @brief CONVEX AREAS PLUS THE OPENINGS BETWEEN THEM -- a navigation mesh, properly.
 *
 * `NavPolygons` turns a tile's cells into convex rectangles. Rectangles alone are a
 * picture, not a mesh: what makes a mesh searchable is knowing, for each one, exactly
 * WHERE a mover may leave it and into which neighbour. That opening is a segment, not a
 * link, and the distinction is the whole reason the good query algorithms are faster and
 * shorter than a graph search.
 *
 * Polyanya (Cui, Harabor & Grastien, IJCAI 2017) searches over intervals of points taken
 * from these segments rather than over discrete nodes, and returns the optimal any-angle
 * path in a single pass. Detour's A*-over-polygons-then-funnel needs the same structure
 * and settles for less: the funnel straightens a path the graph search already committed
 * to, so it cannot recover a shorter route through a different opening.
 *
 * == What a portal is here ==
 *
 * A maximal run of cell boundary along one side of a rectangle where the cell on the
 * other side belongs to one particular neighbour AND the step between them is a step.
 * Both halves matter. Two rectangles can share a hundred cells of border and be joined
 * along only twenty of them -- a ledge that runs beside a drop for most of its length --
 * and a mesh that recorded the whole shared edge as passable would send routes off it.
 *
 * The step test is `Nav::ClimbWindow`, the same function the cell flood, the tile
 * stitcher and the router already use. It has to be: a mesh that opened where the flood
 * did not would let a search cross ground the regions say is separate.
 *
 * == The rim ==
 *
 * A run along the tile's own edge is a portal too, marked `Portal::OUTSIDE`. It is what
 * replaces the baked gateway: a gateway was a run of border cells with a height at each
 * end and a width, and so is this, with the difference that it falls out of the same
 * decomposition as everything else instead of being a second structure with its own
 * rules and its own cost matrix.
 *
 * It still names nothing beyond the tile, and that is deliberate and not an omission. A
 * tile file may never refer to another tile's indices -- re-bake one map and its
 * numbering changes, and every neighbour that had recorded a number into it would be
 * pointing at something else. What lies across the rim is matched by the store when both
 * sides are resident, from the two runs' own geometry.
 */

#include "nav/NavPolygons.hpp"
#include "nav/NavTile.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    /**
     * @brief One opening between two rectangles, as an interval of cell boundary.
     *
     * `side` says which edge of `rect` this is, and therefore which axis `lo`..`hi`
     * runs along:
     *
     *   SIDE_MINUS_X  the edge at x = rect.x0, interval in y
     *   SIDE_PLUS_X   the edge at x = rect.x1 + 1, interval in y
     *   SIDE_MINUS_Y  the edge at y = rect.y0, interval in x
     *   SIDE_PLUS_Y   the edge at y = rect.y1 + 1, interval in x
     *
     * The interval is in CELL indices and inclusive at both ends: cells `lo` through
     * `hi` of this rectangle's border row face the neighbour across a step a mover can
     * take. Turning that into a world segment is `PortalSegment`, which is where the
     * half-cell offsets live so that nothing else has to know them.
     */
    struct Portal
    {
        uint32_t rect = 0;

        /// The rectangle across the opening, or `OUTSIDE` when the opening is the tile's
        /// own rim and what lies beyond it belongs to another file.
        uint32_t neighbour = 0;

        uint8_t side = 0;
        uint16_t lo = 0;
        uint16_t hi = 0;    ///< inclusive

        /// Height at each end of the run, so a match across a tile border can test the
        /// step without either side reading the other's ground.
        float loZ = 0.0f;
        float hiZ = 0.0f;

        /// The narrowest clearance along the run. A mover wider than this cannot use the
        /// opening even where the areas either side suit it.
        uint8_t clearance = 0;

        static constexpr uint32_t OUTSIDE = 0xFFFFFFFFu;

        bool LeavesTheTile() const { return neighbour == OUTSIDE; }

        uint16_t Cells() const { return uint16_t(hi - lo + 1); }
    };

    enum PortalSide : uint8_t
    {
        SIDE_MINUS_X = 0,
        SIDE_PLUS_X = 1,
        SIDE_MINUS_Y = 2,
        SIDE_PLUS_Y = 3,
    };

    /**
     * @brief A hand-authored crossing between two areas of one tile, as the mesh sees it.
     *
     * The jump off the Booty Bay dock; the ledges in Blade's Edge Arena. Three in the
     * whole of 2.4.3, and the dock is a place creatures chase players off -- without it
     * they stop at the edge and evade.
     *
     * == Why this is not a Portal ==
     *
     * A portal is an INTERVAL of shared boundary, and everything Polyanya does with one
     * rests on that: it carries a continuum of routes over the opening and the path stays
     * straight because a straight line between two points of a convex area is inside it.
     * A link has no shared boundary at all. There is a point you leave from, a point you
     * arrive at, and nothing walkable between them -- that is what makes it a link rather
     * than an opening. Giving `Portal` a "kind" byte would have put a thing with no
     * interval into the one structure whose whole meaning is the interval.
     *
     * So it is its own list, the coarse stage steps over it, and the fine stage never
     * sees one: the route is CUT at the mouth, the jump is emitted as a single segment,
     * and Polyanya is asked again from the far mouth. Nothing walks a link, which is the
     * truth about it.
     *
     * Derived from the tile's own `Gateway`/`Link` pair -- the baker still authors them
     * there -- by resolving each mouth's world position to the rectangle covering it.
     */
    struct MeshLink
    {
        uint32_t fromRect = 0;
        uint32_t toRect = 0;

        /// The two mouths, in world yards. Not rectangle centres: a link is authored at
        /// a place, and the place is the lip of the dock rather than the middle of the
        /// area the lip belongs to.
        float fromX = 0.0f;
        float fromY = 0.0f;
        float fromZ = 0.0f;
        float toX = 0.0f;
        float toY = 0.0f;
        float toZ = 0.0f;

        /// What crossing costs, in yards. A jump is not free: priced at zero, a router
        /// prefers a detour through a link to walking three yards round it.
        float cost = 0.0f;

        /// The narrower of the two mouths, in the packed clearance byte.
        uint8_t clearance = 0;

        /// Usable in both directions -- a dock can be jumped off and climbed back onto.
        /// A byte and not a bool because this is written to a file.
        uint8_t bidirectional = 1;

        uint16_t reserved = 0;
    };

    /**
     * @brief A tile's walkable set as convex areas and the openings between them.
     *
     * Built, not stored: the cell grid is still what the file carries, and this is
     * derived from it in one pass. When the mesh becomes the stored form, this struct is
     * what gets written and the derivation disappears -- which is why nothing in it
     * refers back to the cells it came from.
     */
    struct TileMesh
    {
        std::vector<NavRect> rects;

        /// All portals, grouped by rectangle. Rectangle `r` owns
        /// `portals[first[r] .. first[r + 1])`.
        std::vector<Portal> portals;
        std::vector<uint32_t> first;

        /// The hand-authored crossings, ungrouped. Three in the whole of 2.4.3, so a
        /// scan is what this is: an index over a list that short would cost more to keep
        /// correct than it could ever save.
        std::vector<MeshLink> links;

        /**
         * @brief The ground's height, at the terrain's own resolution.
         *
         * HEIGHT_SIDE x HEIGHT_SIDE quantised samples of the LOWEST floor, plus the base
         * they are measured from -- about 33 KB a tile. Areas carry what is constant
         * over an area; this carries what varies smoothly, and the two must not be
         * mixed. Fitting a plane per rectangle instead was measured at fifteen times the
         * geometry, because the ADT heightmap breaks every four nav cells and no plane
         * spans a triangle edge.
         *
         * The lowest floor only. A bridge's height is not a field over the plan -- two
         * floors share a square -- so a rectangle above the ground carries its own
         * `minZ`/`maxZ` and is read from those instead.
         */
        std::vector<uint16_t> heights;
        float baseZ = 0.0f;

        float HeightSample(int hx, int hy) const
        {
            const size_t at = static_cast<size_t>(hx) * HEIGHT_SIDE +
                              static_cast<size_t>(hy);
            return at < heights.size() ? RestoreZ(heights[at], baseZ) : baseZ;
        }

        size_t PortalCount(uint32_t rect) const
        {
            return first[rect + 1] - first[rect];
        }
    };

    /**
     * @brief Derive the mesh of one tile.
     *
     * @param tile   the baked tile, read for its surfaces and its bake parameters
     * @return the rectangles and every opening between them. Portals are symmetric:
     *         a run from A to B has an identical run from B to A.
     */
    TileMesh BuildTileMesh(const NavTile& tile);

    /**
     * @brief The world-space segment a portal spans, on the tile it belongs to.
     *
     * The interval is a run of cells, and the opening is the boundary BETWEEN those
     * cells and their neighbours: it starts at the outer rim of cell `lo` and ends at
     * the outer rim of cell `hi`, which is half a cell beyond each centre. Getting that
     * wrong shortens every doorway in the world by one cell and is invisible until a
     * route refuses a gap it fits through.
     *
     * @param[out] ax,ay,bx,by the two ends, in world yards.
     */
    void PortalSegment(const NavTile& tile, const TileMesh& mesh, const Portal& portal,
                       float& ax, float& ay, float& bx, float& by);

    /**
     * @brief Which rectangle covers a world position, or -1.
     *
     * A scan, deliberately. A cell-to-rectangle map is a megabyte per tile against a
     * couple of thousand rectangles to walk, and this is asked twice per query.
     */
    int32_t RectAt(const NavTile& tile, const TileMesh& mesh, float x, float y);

    /**
     * @brief The rectangle a body at `z` is standing on, not merely one above or below.
     *
     * `RectAt` answers in PLAN, and with stacked floors that is ambiguous: a bridge and
     * the ground beneath it cover the same square, and taking the first match means
     * taking whichever the decomposition happened to emit first -- the lower one. A
     * query that starts on an upper floor then begins its search on the floor below,
     * which is not a worse answer, it is a different place.
     *
     * Blackrock Depths is where this shows and open terrain never can: two points in one
     * tile and one region, sixty yards apart in height, came back unroutable because
     * both had been resolved onto the lowest floor under them.
     *
     * @return The nearest rectangle at or below `z`, or -1 when nothing covers the point.
     */
    int32_t RectAtHeight(const NavTile& tile, const TileMesh& mesh, float x, float y,
                         float z);

    /**
     * @brief The ground's height at a world position, from the mesh alone.
     *
     * This is the query that decides whether the cell grid can go. Every consumer that
     * asks "what is the floor here" -- seating a route's points, placing a mover,
     * answering a spell's range check -- goes through it, and it reads a plane fitted at
     * bake time with a bounded residual rather than a quarter of a million samples.
     *
     * @return False when nothing walkable covers the position.
     */
    bool MeshHeightAt(const NavTile& tile, const TileMesh& mesh, float x, float y,
                      float& outZ);

    /**
     * @brief The walkable surface a body at `z` is standing on, from the mesh alone.
     *
     * What `NavStore::SurfaceAt` becomes. It answers with everything a `MoveProfile`
     * needs to judge it -- height, region, AREA and clearance -- which is why the area
     * had to go onto the rectangle: without it a swimmer and a walker are the same
     * mover, and the profile that distinguishes them has nothing to read.
     *
     * Every rectangle covering the square is a candidate, so a bridge and the ground
     * under it are both offered and the nearer one below the body wins. That is the
     * stacked-floor case the cell version handled by walking a cell's surface list.
     */
    bool MeshSurfaceAt(const NavTile& tile, const TileMesh& mesh, float x, float y,
                       float z, float tolerance, Surface& out);

    /// The world position of a rectangle's middle. What a coarse search measures
    /// between: an area is a place, and its centre is the one point that stands for it
    /// without favouring either end.
    void RectCentre(const NavTile& tile, const NavRect& rect, float& x, float& y);

    /**
     * @brief One matched crossing between two tiles, in world terms.
     *
     * Produced by the store when both sides are resident, from the two rim portals'
     * own geometry -- neither file ever recorded anything about the other. This is what
     * replaces the baked gateway crossing table.
     */
    struct MeshCrossing
    {
        int32_t nearTileX = 0;
        int32_t nearTileY = 0;
        uint32_t nearRect = 0;

        int32_t farTileX = 0;
        int32_t farTileY = 0;
        uint32_t farRect = 0;

        /**
         * @brief Where the crossing is on the NEAR side, in world yards.
         *
         * Strictly inside the near tile -- it is the centre of that tile's own border
         * cell. This is where a route LEAVES from, and it is not a place the far tile
         * contains.
         */
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        /**
         * @brief And where it arrives, on the far side. One cell across.
         *
         * Two points and not one, because a border is exactly the place where "where I
         * left" and "where I arrived" stop being the same fact. With only the near point
         * a router hands the far tile's search a start it does not contain, the search
         * refuses it, and no route between two tiles is ever produced.
         */
        float farX = 0.0f;
        float farY = 0.0f;
        float farZ = 0.0f;

        /// The narrowest cell on the emitted run, in the packed clearance byte. A
        /// crossing is a door, not a field: the mover has to fit through every cell
        /// of the run that was kept.
        uint8_t clearance = 0;
    };

    /**
     * @brief Match one tile's rim against a neighbour's, both ways.
     *
     * Two rim runs join where they face each other along the shared border AND the step
     * between them is a step -- the same `ClimbWindow` the bake linked cells with, taken
     * at the stricter of the two tiles' parameters. A pair of tiles baked by different
     * runs may disagree, and the smaller limit is the one that cannot invent a step
     * neither bake believed in.
     *
     * @param out crossings are APPENDED, in both directions.
     */
    void MatchRims(const NavTile& nearTile, const TileMesh& nearMesh,
                   const NavTile& farTile, const TileMesh& farMesh,
                   std::vector<MeshCrossing>& out);
}
