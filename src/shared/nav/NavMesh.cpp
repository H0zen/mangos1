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

#include "nav/NavMesh.hpp"

#include "nav/NavGrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nav
{
    namespace
    {
        constexpr int SIDE = CELLS_PER_TILE;

        /// The cell just outside a rectangle's edge, at position `along` on that edge.
        /// Returns false when the edge is the tile's own rim: a portal never leaves the
        /// tile, because what joins two tiles is matched by the store when both are
        /// resident and neither file may refer to the other's indices.
        bool OutsideCell(const NavRect& rect, uint8_t side, int along, int& outX,
                         int& outY, int& inX, int& inY)
        {
            switch (side)
            {
                case SIDE_MINUS_X:
                    inX = rect.x0;
                    inY = along;
                    outX = inX - 1;
                    outY = along;
                    break;
                case SIDE_PLUS_X:
                    inX = rect.x1;
                    inY = along;
                    outX = inX + 1;
                    outY = along;
                    break;
                case SIDE_MINUS_Y:
                    inX = along;
                    inY = rect.y0;
                    outX = along;
                    outY = inY - 1;
                    break;
                default:
                    inX = along;
                    inY = rect.y1;
                    outX = along;
                    outY = inY + 1;
                    break;
            }

            return outX >= 0 && outX < SIDE && outY >= 0 && outY < SIDE;
        }

        void EdgeRange(const NavRect& rect, uint8_t side, int& lo, int& hi)
        {
            const bool alongY = side == SIDE_MINUS_X || side == SIDE_PLUS_X;
            lo = alongY ? rect.y0 : rect.x0;
            hi = alongY ? rect.y1 : rect.x1;
        }
    }

    TileMesh BuildTileMesh(const NavTile& tile)
    {
        // Every floor, lowest first. A bridge over ground is two walkable surfaces in
        // one square of plan, and a rectangle can only be one of them -- so each plan is
        // decomposed on its own and the rectangles are joined afterwards by HEIGHT, not
        // by which position in a cell's stack they happened to occupy.
        const std::vector<TilePlan> plans = ReadTilePlans(tile);

        TileMesh mesh;
        if (plans.empty())
        {
            mesh.first.assign(1, 0);
            return mesh;
        }

        // One cell-to-rectangle map per floor, and the rectangle indices are global
        // across floors, so a portal can join a ramp on the ground to the bridge it
        // climbs onto without either side knowing which plan the other came from.
        std::vector<std::vector<int32_t>> cellToRect(plans.size());

        for (size_t layer = 0; layer < plans.size(); ++layer)
        {
            const uint32_t base = static_cast<uint32_t>(mesh.rects.size());

            std::vector<NavRect> here =
                DecomposeTile(tile, plans[layer], &cellToRect[layer]);

            for (int32_t& at : cellToRect[layer])
            {
                if (at >= 0)
                {
                    at += static_cast<int32_t>(base);
                }
            }

            mesh.rects.insert(mesh.rects.end(), here.begin(), here.end());
        }

        // The height field, at the terrain's own resolution and from the LOWEST floor.
        // Four nav cells to a height cell, so the sample at a height corner is the nav
        // cell whose corner it is -- read, not resampled.
        mesh.baseZ = tile.BaseZ();
        mesh.heights.assign(static_cast<size_t>(HEIGHT_SIDE) * HEIGHT_SIDE, 0);

        for (int hx = 0; hx < HEIGHT_SIDE; ++hx)
        {
            for (int hy = 0; hy < HEIGHT_SIDE; ++hy)
            {
                const int cx = std::min(hx * 4, SIDE - 1);
                const int cy = std::min(hy * 4, SIDE - 1);
                const size_t cell = static_cast<size_t>(cx) * SIDE +
                                    static_cast<size_t>(cy);

                const float z = plans[0].Walkable(static_cast<int>(cell))
                                    ? plans[0].z[cell]
                                    : mesh.baseZ;

                mesh.heights[static_cast<size_t>(hx) * HEIGHT_SIDE +
                             static_cast<size_t>(hy)] = QuantiseZ(z, mesh.baseZ);
            }
        }

        mesh.first.assign(mesh.rects.size() + 1, 0);

        // The one step rule, shared with the flood that built the regions, with the tile
        // stitcher and with the router. A mesh that opened where they did not would let a
        // search cross ground they call separate.
        const float window = ClimbWindow(tile.Params().maxClimb,
                                         tile.Params().maxSlopeDeg, CELL_SIZE);

        for (size_t r = 0; r < mesh.rects.size(); ++r)
        {
            const NavRect& rect = mesh.rects[r];
            mesh.first[r] = static_cast<uint32_t>(mesh.portals.size());

            // The floor this rectangle came from, and the map of who owns what on it.
            const TilePlan& plan = plans[rect.layer];

            for (uint8_t side = 0; side < 4; ++side)
            {
                int lo = 0;
                int hi = 0;
                EdgeRange(rect, side, lo, hi);

                // Runs are accumulated rather than emitted per cell: a doorway is one
                // opening however many cells wide it is, and the interval is what the
                // search interpolates over.
                int64_t runNeighbour = -1;
                int runFrom = 0;

                const auto flush = [&](int runTo)
                {
                    if (runNeighbour < 0)
                    {
                        return;
                    }

                    Portal portal;
                    portal.rect = static_cast<uint32_t>(r);
                    portal.neighbour = static_cast<uint32_t>(runNeighbour);
                    portal.side = side;
                    portal.lo = static_cast<uint16_t>(runFrom);
                    portal.hi = static_cast<uint16_t>(runTo);

                    // Everything a match across a tile border needs, taken from this
                    // side's own cells: the height at each end of the run and the
                    // narrowest room along it. The neighbour computes the same three
                    // from its own, and the two are compared without either file having
                    // recorded anything about the other.
                    const bool alongY = side == SIDE_MINUS_X || side == SIDE_PLUS_X;
                    const int fixed = (side == SIDE_MINUS_X)   ? rect.x0
                                      : (side == SIDE_PLUS_X)  ? rect.x1
                                      : (side == SIDE_MINUS_Y) ? rect.y0
                                                               : rect.y1;

                    const auto cellOf = [&](int at)
                    {
                        return alongY ? static_cast<size_t>(fixed) * SIDE +
                            static_cast<size_t>(at)
                                      : static_cast<size_t>(at) * SIDE +
                                          static_cast<size_t>(fixed);
                    };

                    portal.loZ = plan.z[cellOf(runFrom)];
                    portal.hiZ = plan.z[cellOf(runTo)];

                    uint8_t narrowest = 0xFF;
                    for (int at = runFrom; at <= runTo; ++at)
                    {
                        const size_t cell = cellOf(at);
                        const Surface surface = tile.SurfaceAt(static_cast<int>(cell),
                                                               plan.layer[cell]);
                        if (surface.Valid())
                        {
                            narrowest = std::min(narrowest, surface.clearance);
                        }
                    }
                    portal.clearance = narrowest;

                    mesh.portals.push_back(portal);
                    runNeighbour = -1;
                };

                for (int along = lo; along <= hi; ++along)
                {
                    int outX = 0, outY = 0, inX = 0, inY = 0;
                    int64_t neighbour = -1;

                    if (OutsideCell(rect, side, along, outX, outY, inX, inY))
                    {
                        const size_t inside = static_cast<size_t>(inX) * SIDE +
                            static_cast<size_t>(inY);
                        const size_t outside = static_cast<size_t>(outX) * SIDE +
                            static_cast<size_t>(outY);

                        const bool joined =
                            plan.Walkable(static_cast<int>(inside)) &&
                            plan.Walkable(static_cast<int>(outside)) &&
                            plan.region[inside] == plan.region[outside] &&
                            std::fabs(plan.z[inside] - plan.z[outside]) <= window;

                        if (joined)
                        {
                            neighbour = cellToRect[rect.layer][outside];
                        }

                        // A ramp climbing onto a bridge is two rectangles on DIFFERENT
                        // floors, joined where the step between them is a step. Looked
                        // for across every floor, because which position a surface holds
                        // in its own cell's stack says nothing about which it holds in
                        // the neighbour's.
                        for (size_t other = 0;
                             other < plans.size() && neighbour < 0; ++other)
                        {
                            if (other == rect.layer)
                            {
                                continue;
                            }

                            const TilePlan& up = plans[other];
                            if (!up.Walkable(static_cast<int>(outside)) ||
                                up.region[outside] != plan.region[inside])
                            {
                                continue;
                            }

                            if (std::fabs(plan.z[inside] - up.z[outside]) <= window)
                            {
                                neighbour = cellToRect[other][outside];
                            }
                        }
                    }
                    else
                    {
                        // Off the padded grid: this run is on the tile's own rim. It is
                        // an opening -- something walks across it -- and the store is
                        // what finds out into what.
                        const size_t inside = static_cast<size_t>(inX) * SIDE +
                            static_cast<size_t>(inY);
                        if (plan.Walkable(static_cast<int>(inside)))
                        {
                            neighbour = static_cast<int64_t>(Portal::OUTSIDE);
                        }
                    }

                    if (neighbour != runNeighbour)
                    {
                        flush(along - 1);
                        if (neighbour >= 0)
                        {
                            runNeighbour = neighbour;
                            runFrom = along;
                        }
                    }
                }

                flush(hi);
            }
        }

        mesh.first[mesh.rects.size()] = static_cast<uint32_t>(mesh.portals.size());

        // === The hand-authored crossings, moved onto the areas.
        //
        // The baker still authors a link as an edge between two GATEWAYS, because that is
        // what `offmesh.txt` describes and what the tile file carries. What the mesh
        // needs is which AREA each mouth stands in, and that is a lookup rather than a second
        // authored fact: the mouth is a world position, and the rectangle covering it at
        // that height is the area a mover jumping from there is leaving.
        //
        // By HEIGHT and not in plan. A dock is over water and a ledge is over a floor, so
        // taking the first rectangle covering the square would resolve half the links in
        // the game onto the ground underneath the thing they are authored on.
        for (const Link& link : tile.Links())
        {
            if (link.fromGate >= tile.Gateways().size() ||
                link.toGate >= tile.Gateways().size())
            {
                continue;
            }

            const Gateway& fromGate = tile.Gateways()[link.fromGate];
            const Gateway& toGate = tile.Gateways()[link.toGate];

            // A link joins two MOUTHS. An edge between border gateways would be a claim
            // about ground in another file, which no tile is allowed to make.
            if (!fromGate.IsLink() || !toGate.IsLink())
            {
                continue;
            }

            const int32_t fromRect =
                RectAtHeight(tile, mesh, fromGate.x, fromGate.y, fromGate.z);
            const int32_t toRect =
                RectAtHeight(tile, mesh, toGate.x, toGate.y, toGate.z);
            if (fromRect < 0 || toRect < 0 || fromRect == toRect)
            {
                // Nothing under a mouth, or both mouths in one area -- in which case the
                // ground already joins them and the jump is not needed.
                continue;
            }

            MeshLink out;
            out.fromRect = static_cast<uint32_t>(fromRect);
            out.toRect = static_cast<uint32_t>(toRect);
            out.fromX = fromGate.x;
            out.fromY = fromGate.y;
            out.fromZ = fromGate.z;
            out.toX = toGate.x;
            out.toY = toGate.y;
            out.toZ = toGate.z;
            out.cost = link.cost;
            out.clearance = QuantiseClearance(std::min(fromGate.width, toGate.width));
            out.bidirectional = link.bidirectional ? 1 : 0;

            mesh.links.push_back(out);
        }

        return mesh;
    }

    int32_t RectAt(const NavTile& tile, const TileMesh& mesh, float x, float y)
    {
        const int gx = CellIndex(x);
        const int gy = CellIndex(y);
        if (TileOfCell(gx) != tile.TileX() || TileOfCell(gy) != tile.TileY())
        {
            return -1;
        }

        const int lx = LocalOfCell(gx);
        const int ly = LocalOfCell(gy);

        for (size_t i = 0; i < mesh.rects.size(); ++i)
        {
            const NavRect& r = mesh.rects[i];
            if (lx >= r.x0 && lx <= r.x1 && ly >= r.y0 && ly <= r.y1)
            {
                return static_cast<int32_t>(i);
            }
        }

        return -1;
    }

    int32_t RectAtHeight(const NavTile& tile, const TileMesh& mesh, float x, float y,
                         float z)
    {
        const int gx = CellIndex(x);
        const int gy = CellIndex(y);
        if (TileOfCell(gx) != tile.TileX() || TileOfCell(gy) != tile.TileY())
        {
            return -1;
        }

        const int lx = LocalOfCell(gx);
        const int ly = LocalOfCell(gy);

        int32_t best = -1;
        float bestDelta = 0.0f;

        for (size_t i = 0; i < mesh.rects.size(); ++i)
        {
            const NavRect& rect = mesh.rects[i];
            if (lx < rect.x0 || lx > rect.x1 || ly < rect.y0 || ly > rect.y1)
            {
                continue;
            }

            float here = 0.0f;
            if (rect.layer != 0)
            {
                here = (rect.minZ + rect.maxZ) * 0.5f;
            }
            else if (!MeshHeightAt(tile, mesh, x, y, here))
            {
                continue;
            }

            // Below the body is what it stands on; above it is a ceiling, and only a
            // near one counts at all. The same preference the cell version applied when
            // it walked a cell's surface list.
            const float delta = here <= z ? z - here : (here - z) * 2.0f;
            if (best < 0 || delta < bestDelta)
            {
                bestDelta = delta;
                best = static_cast<int32_t>(i);
            }
        }

        return best;
    }

    bool MeshHeightAt(const NavTile& tile, const TileMesh& mesh, float x, float y,
                      float& outZ)
    {
        const int32_t at = RectAt(tile, mesh, x, y);
        if (at < 0)
        {
            return false;
        }

        const NavRect& rect = mesh.rects[static_cast<size_t>(at)];

        // A floor above the ground is not a field over the plan -- two of them share the
        // square -- so it answers from its own recorded range instead. Flat by
        // construction within a rectangle's tolerance, which is what a bridge deck is.
        if (rect.layer != 0)
        {
            outZ = (rect.minZ + rect.maxZ) * 0.5f;
            return true;
        }

        // The ground, from the height field, bilinearly. In HEIGHT-cell coordinates,
        // unrounded, so a mover crossing a cell rim does not see the floor step.
        const float cellX = CellCoord(x) -
                            static_cast<float>(GlobalCell(tile.TileX(), 0));
        const float cellY = CellCoord(y) -
                            static_cast<float>(GlobalCell(tile.TileY(), 0));

        const float hx = cellX * 0.25f;
        const float hy = cellY * 0.25f;

        const int ix = std::max(0, std::min(static_cast<int>(hx), HEIGHT_SIDE - 2));
        const int iy = std::max(0, std::min(static_cast<int>(hy), HEIGHT_SIDE - 2));

        const float fx = std::max(0.0f, std::min(hx - static_cast<float>(ix), 1.0f));
        const float fy = std::max(0.0f, std::min(hy - static_cast<float>(iy), 1.0f));

        const float a = mesh.HeightSample(ix, iy);
        const float b = mesh.HeightSample(ix + 1, iy);
        const float c = mesh.HeightSample(ix, iy + 1);
        const float d = mesh.HeightSample(ix + 1, iy + 1);

        outZ = (a * (1.0f - fx) + b * fx) * (1.0f - fy) +
               (c * (1.0f - fx) + d * fx) * fy;
        return true;
    }

    bool MeshSurfaceAt(const NavTile& tile, const TileMesh& mesh, float x, float y,
                       float z, float tolerance, Surface& out)
    {
        out = Surface();

        const int gx = CellIndex(x);
        const int gy = CellIndex(y);
        if (TileOfCell(gx) != tile.TileX() || TileOfCell(gy) != tile.TileY())
        {
            return false;
        }

        const int lx = LocalOfCell(gx);
        const int ly = LocalOfCell(gy);

        // The floor a body at this height is standing ON: the nearest within tolerance,
        // preferring one at or below it, because a unit is on the floor it is above and
        // not the ceiling it is under. Every rectangle covering the square is a
        // candidate, which is exactly the stacked-floor case the cell version handled by
        // walking a cell's surface list.
        float best = tolerance;
        bool found = false;

        for (size_t i = 0; i < mesh.rects.size(); ++i)
        {
            const NavRect& rect = mesh.rects[i];
            if (lx < rect.x0 || lx > rect.x1 || ly < rect.y0 || ly > rect.y1)
            {
                continue;
            }

            float here = 0.0f;
            if (rect.layer != 0)
            {
                here = (rect.minZ + rect.maxZ) * 0.5f;
            }
            else if (!MeshHeightAt(tile, mesh, x, y, here))
            {
                continue;
            }

            // Below the body counts fully; above it only within the tolerance, and the
            // nearer of the two wins.
            const float delta = here <= z ? z - here : (here - z) * 2.0f;
            if (delta > best)
            {
                continue;
            }

            best = delta;
            found = true;

            out.z = here;
            out.region = rect.region;
            out.area = rect.area;
            out.layer = rect.layer;

            // The rectangle's own figure, and the caller must treat it as an upper
            // bound rather than as this point's room. A maximal rectangle spans open
            // ground and its own rim, so no single number describes both -- which is
            // why the width test belongs at the point and not here.
            out.clearance = rect.clearance;
        }

        return found;
    }

    void RectCentre(const NavTile& tile, const NavRect& rect, float& x, float& y)
    {
        const float midX = (static_cast<float>(rect.x0) +
                            static_cast<float>(rect.x1)) * 0.5f;
        const float midY = (static_cast<float>(rect.y0) +
                            static_cast<float>(rect.y1)) * 0.5f;

        // CellCentre takes a whole index; a rectangle's middle falls between two of them
        // whenever it spans an even number of cells, so the half is added in world yards
        // -- and subtracted, because cell indices grow as world coordinates fall.
        x = CellCentre(GlobalCell(tile.TileX(), static_cast<int>(midX))) -
            (midX - std::floor(midX)) * CELL_SIZE;
        y = CellCentre(GlobalCell(tile.TileY(), static_cast<int>(midY))) -
            (midY - std::floor(midY)) * CELL_SIZE;
    }

    void MatchRims(const NavTile& nearTile, const TileMesh& nearMesh,
                   const NavTile& farTile, const TileMesh& farMesh,
                   std::vector<MeshCrossing>& out)
    {
        const int dx = farTile.TileX() - nearTile.TileX();
        const int dy = farTile.TileY() - nearTile.TileY();

        // Orthogonal neighbours only. A diagonal shares one corner and no run, so there
        // is nothing to match and pretending otherwise would invent a crossing through
        // the point where four tiles meet.
        if ((dx != 0) == (dy != 0))
        {
            return;
        }

        // Which rim of each tile faces the other. Cell indices grow as world coordinates
        // fall, so the neighbour at tileX + 1 lies at SMALLER world x -- which is why
        // this mapping is written out rather than inferred at the call site.
        const uint8_t nearSide = dx == 1    ? SIDE_PLUS_X
                                 : dx == -1 ? SIDE_MINUS_X
                                 : dy == 1  ? SIDE_PLUS_Y
                                            : SIDE_MINUS_Y;
        const uint8_t farSide = dx == 1    ? SIDE_MINUS_X
                                : dx == -1 ? SIDE_PLUS_X
                                : dy == 1  ? SIDE_MINUS_Y
                                           : SIDE_PLUS_Y;

        const float window =
            ClimbWindow(std::min(nearTile.Params().maxClimb, farTile.Params().maxClimb),
                        std::min(nearTile.Params().maxSlopeDeg,
                                 farTile.Params().maxSlopeDeg),
                        CELL_SIZE);

        for (const Portal& here : nearMesh.portals)
        {
            if (!here.LeavesTheTile() || here.side != nearSide)
            {
                continue;
            }

            for (const Portal& there : farMesh.portals)
            {
                if (!there.LeavesTheTile() || there.side != farSide)
                {
                    continue;
                }

                // The two runs are indexed along the same axis and the border is shared,
                // so they overlap exactly where their intervals do.
                const uint16_t lo = std::max(here.lo, there.lo);
                const uint16_t hi = std::min(here.hi, there.hi);
                if (lo > hi)
                {
                    continue;
                }

                // Heights are linear along a run by construction -- a rectangle's ground
                // is a plane -- so the height at any index of it is a lerp of its ends.
                const auto heightAt = [](const Portal& portal, uint16_t at)
                {
                    if (portal.hi == portal.lo)
                    {
                        return portal.loZ;
                    }
                    const float t = static_cast<float>(at - portal.lo) /
                                    static_cast<float>(portal.hi - portal.lo);
                    return portal.loZ + (portal.hiZ - portal.loZ) * t;
                };

                // === WHERE the step is a step, not merely WHETHER it is somewhere.
                //
                // Testing only the two ends and keeping the whole overlap was wrong in
                // the case that matters: a run whose one end is a doorstep and whose
                // other is a cliff passed, and the entire overlap became a crossing.
                // The search then interpolates along all of it and can step off the
                // cliff -- the exact failure the per-cell step test exists to prevent,
                // reintroduced at the one place two tiles meet.
                //
                // So walk the overlap and keep the contiguous runs that ARE steps. A
                // scan rather than algebra: the difference is |linear| and therefore has
                // one admissible interval, but a run is at most 512 cells and this is
                // paid once when two tiles are stitched, so the version that cannot be
                // got wrong is the one to write.
                const bool alongY =
                    nearSide == SIDE_MINUS_X || nearSide == SIDE_PLUS_X;

                const NavRect& nearRect = nearMesh.rects[here.rect];
                const NavRect& farRect = farMesh.rects[there.rect];

                const int nearFixed = nearSide == SIDE_MINUS_X   ? nearRect.x0
                                      : nearSide == SIDE_PLUS_X  ? nearRect.x1
                                      : nearSide == SIDE_MINUS_Y ? nearRect.y0
                                                                 : nearRect.y1;
                const int farFixed = farSide == SIDE_MINUS_X   ? farRect.x0
                                     : farSide == SIDE_PLUS_X  ? farRect.x1
                                     : farSide == SIDE_MINUS_Y ? farRect.y0
                                                               : farRect.y1;

                const auto emit = [&](uint16_t from, uint16_t to)
                {
                    MeshCrossing crossing;
                    crossing.nearTileX = nearTile.TileX();
                    crossing.nearTileY = nearTile.TileY();
                    crossing.nearRect = here.rect;
                    crossing.farTileX = farTile.TileX();
                    crossing.farTileY = farTile.TileY();
                    crossing.farRect = there.rect;
                    // The run we are emitting, not the whole portal. A door next to a
                    // crack on the same rectangle edge used to inherit the crack.
                    uint8_t narrowest = 0xFF;
                    for (int at = int(from); at <= int(to); ++at)
                    {
                        const int nearIn = alongY
                                               ? nearFixed * SIDE + at
                                               : at * SIDE + nearFixed;
                        const int farIn = alongY
                                              ? farFixed * SIDE + at
                                              : at * SIDE + farFixed;

                        const auto pick = [](const NavTile& tile, int inTile,
                                             float height) -> uint8_t
                        {
                            std::vector<Surface> surfaces;
                            tile.SurfacesAt(inTile, surfaces);
                            float best = std::numeric_limits<float>::max();
                            uint8_t clearance = 0xFF;
                            for (const Surface& s : surfaces)
                            {
                                const float delta = std::fabs(s.z - height);
                                if (delta < best)
                                {
                                    best = delta;
                                    clearance = s.clearance;
                                }
                            }
                            return clearance;
                        };

                        narrowest = std::min(
                            narrowest, pick(nearTile, nearIn, heightAt(here, uint16_t(at))));
                        narrowest = std::min(
                            narrowest, pick(farTile, farIn, heightAt(there, uint16_t(at))));
                    }

                    crossing.clearance = narrowest;

                    const uint16_t middle = static_cast<uint16_t>((from + to) / 2);

                    // TWO POINTS, ONE ON EACH SIDE. A crossing used to carry only the
                    // near tile's cell centre, which lies strictly inside the near tile
                    // -- so a router that used it as the place the next tile's search
                    // BEGINS handed that search a point the far tile does not contain,
                    // and every route between two tiles failed at its first border.
                    // Where you leave from and where you arrive are two facts, and a
                    // border is precisely the place they differ.
                    crossing.x = CellCentre(GlobalCell(nearTile.TileX(),
                                                       alongY ? nearFixed : middle));
                    crossing.y = CellCentre(GlobalCell(nearTile.TileY(),
                                                       alongY ? middle : nearFixed));
                    crossing.z = heightAt(here, middle);

                    crossing.farX = CellCentre(GlobalCell(farTile.TileX(),
                                                          alongY ? farFixed : middle));
                    crossing.farY = CellCentre(GlobalCell(farTile.TileY(),
                                                          alongY ? middle : farFixed));
                    crossing.farZ = heightAt(there, middle);

                    out.push_back(crossing);
                };

                int runFrom = -1;
                for (int at = lo; at <= int(hi); ++at)
                {
                    const uint16_t index = static_cast<uint16_t>(at);
                    const bool step =
                        std::fabs(heightAt(here, index) - heightAt(there, index)) <=
                        window;

                    if (step && runFrom < 0)
                    {
                        runFrom = at;
                    }
                    else if (!step && runFrom >= 0)
                    {
                        emit(static_cast<uint16_t>(runFrom),
                             static_cast<uint16_t>(at - 1));
                        runFrom = -1;
                    }
                }

                if (runFrom >= 0)
                {
                    emit(static_cast<uint16_t>(runFrom), hi);
                }
            }
        }
    }

    void PortalSegment(const NavTile& tile, const TileMesh& mesh, const Portal& portal,
                       float& ax, float& ay, float& bx, float& by)
    {
        const NavRect& rect = mesh.rects[portal.rect];
        const bool alongY = portal.side == SIDE_MINUS_X || portal.side == SIDE_PLUS_X;

        // Cell indices grow as world coordinates fall, so the low index is the HIGH
        // world coordinate and the outer rim of it is half a cell further out still.
        const int globalLo =
            alongY ? GlobalCell(tile.TileY(), portal.lo) : GlobalCell(tile.TileX(), portal.lo);
        const int globalHi =
            alongY ? GlobalCell(tile.TileY(), portal.hi) : GlobalCell(tile.TileX(), portal.hi);

        const float from = CellCentre(globalLo) + CELL_SIZE * 0.5f;
        const float to = CellCentre(globalHi) - CELL_SIZE * 0.5f;

        // The fixed axis is the boundary the run lies on: between the rectangle's border
        // row and the row outside it, so half a cell beyond that row's centre.
        float fixed = 0.0f;
        switch (portal.side)
        {
            case SIDE_MINUS_X:
                fixed = CellCentre(GlobalCell(tile.TileX(), rect.x0)) + CELL_SIZE * 0.5f;
                break;
            case SIDE_PLUS_X:
                fixed = CellCentre(GlobalCell(tile.TileX(), rect.x1)) - CELL_SIZE * 0.5f;
                break;
            case SIDE_MINUS_Y:
                fixed = CellCentre(GlobalCell(tile.TileY(), rect.y0)) + CELL_SIZE * 0.5f;
                break;
            default:
                fixed = CellCentre(GlobalCell(tile.TileY(), rect.y1)) - CELL_SIZE * 0.5f;
                break;
        }

        if (alongY)
        {
            ax = fixed;
            bx = fixed;
            ay = from;
            by = to;
        }
        else
        {
            ay = fixed;
            by = fixed;
            ax = from;
            bx = to;
        }
    }
}
