#include "nav/Router.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Nav
{
    namespace
    {
        constexpr float INF = std::numeric_limits<float>::max();

        constexpr int DIRS = 8;
        constexpr int DX[DIRS] = {-1, 1, 0, 0, -1, -1, 1, 1};
        constexpr int DY[DIRS] = {0, 0, -1, 1, -1, 1, -1, 1};

        inline float StepLength(int dir)
        {
            return dir < 4 ? CELL_SIZE : CELL_SIZE * 1.41421356f;
        }

        /**
         * @brief Layers of one cell the fine search will consider.
         *
         * A bound is needed only because a search node is a cell and a layer packed
         * into one integer. It costs nothing to make it generous: a tile has 2^18
         * cells, so six bits of layer still leaves the key inside 24 bits.
         *
         * It has to be generous. The first cut at this was FOUR, on the reasoning that
         * nothing stacks more floors than that under one square of ground -- which is
         * simply false for the places that need routing most. Blackrock Depths,
         * Blackrock Spire, Karazhan and the wells of Ironforge all pile up well past
         * four, and the failure would have been quiet: the data still holds every
         * surface, so nothing looks missing, and only the upper floors are unroutable.
         */
        constexpr uint16_t MAX_SEARCH_LAYERS = 64;
        constexpr int LAYER_SHIFT = 6;

        /// A node of the fine search: a cell of one tile, and which of its surfaces.
        inline uint32_t NodeKey(int inTile, uint16_t layer)
        {
            return (uint32_t(inTile) << LAYER_SHIFT) | uint32_t(layer);
        }

        inline int KeyCell(uint32_t key) { return int(key >> LAYER_SHIFT); }

        inline int LocalX(int inTile) { return inTile / CELLS_PER_TILE; }
        inline int LocalY(int inTile) { return inTile % CELLS_PER_TILE; }

        inline int InTileOf(int localX, int localY)
        {
            return localX * CELLS_PER_TILE + localY;
        }

        /// World position of an in-tile cell's centre.
        Geometry::Vector3 CellWorld(const NavTile& tile, int inTile, float z)
        {
            const float x = CellCentre(GlobalCell(tile.TileX(), LocalX(inTile)));
            const float y = CellCentre(GlobalCell(tile.TileY(), LocalY(inTile)));
            return Geometry::Vector3(x, y, z);
        }

        float Dist2D(const Geometry::Vector3& a, const Geometry::Vector3& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        /**
         * @brief What crossing one cell costs a mover, as a multiplier.
         *
         * Above one for ground that is passable but unpleasant. The rim penalty is
         * small on purpose: it is enough to keep a creature off the parapet of a bridge
         * when the middle is free, and not enough to make it refuse a ledge that is the
         * only way through.
         */
        float CellPenalty(const MoveProfile& profile, uint8_t packed)
        {
            float cost = profile.CostOf(AreaOf(packed));
            const uint8_t flags = FlagsOf(packed);

            if (flags & CELL_STEEP)
            {
                cost *= 2.0f;
            }
            if (flags & CELL_BORDER)
            {
                cost *= 1.15f;
            }

            return cost;
        }

        /// May this mover stand on this surface at all?
        bool Admits(const MoveProfile& profile, const Surface& surface)
        {
            if (!surface.Valid() || !profile.Admits(surface.area))
            {
                return false;
            }
            if (!profile.Fits(surface.clearance))
            {
                return false;
            }

            // A mover with feet keeps them on the floor. The baked data offers a
            // swimmer's surface and the seabed under it as two separate places, and
            // without this a walking creature crossing a bay would surface halfway.
            if (AreaOf(surface.area) == NavArea::Water && profile.canWalk &&
                !profile.canSwim)
            {
                return false;
            }

            return true;
        }

        /**
         * @brief The surface of a neighbouring cell this mover would step onto.
         *
         * The nearest by height within the climb limit, which is the same rule the
         * baker connected its own graph with. Nearest and not lowest: stepping off a
         * balcony, the cell in front holds both the balcony's continuation and the
         * ground two storeys down, and only one of those is a step.
         */
        bool StepTo(const NavTile& tile, int inTile, float fromZ,
                    const MoveProfile& profile, float climb, Surface& out)
        {
            // Reused rather than built. This is the innermost thing the search does --
            // eight times per cell expanded, tens of thousands of times per route --
            // and a fresh vector here was an allocation and a free on every one of
            // them. thread_local because the offline baker runs several searches at
            // once; the server's own routing is all on the world thread.
            static thread_local std::vector<Surface> surfaces;
            tile.SurfacesAt(inTile, surfaces);

            float bestDelta = climb;
            bool found = false;

            for (const Surface& s : surfaces)
            {
                if (s.layer >= MAX_SEARCH_LAYERS || !Admits(profile, s))
                {
                    continue;
                }

                const float delta = std::fabs(s.z - fromZ);
                if (delta <= bestDelta)
                {
                    bestDelta = delta;
                    out = s;
                    found = true;
                }
            }

            return found;
        }

        /**
         * @brief EVERY surface of a cell this mover could step onto, not just the
         *        nearest one.
         *
         * The fine search needs all of them, and the reason is a mismatch that would
         * otherwise be invisible. The baker's graph is UNDIRECTED: it links a to b, then
         * fills in b to a wherever the nearest-surface rule did not find it, because a
         * region flood and a Dijkstra both assume a step that exists one way exists the
         * other. If the search then admitted only the nearest surface, it would refuse
         * steps the region flood had already counted -- so the coarse stage would plan
         * through a region it believes is connected and the fine stage would report a
         * wall in the middle of it. The route fails, the mover falls back to a straight
         * line, and nothing in the logs says why.
         *
         * Taking every surface within the climb limit is the rule that IS symmetric:
         * if b is within reach of a, then a is within reach of b, whatever else is
         * stacked in either cell.
         *
         * @param out Cleared, then filled nearest-first. Nearest-first matters: the
         *            search expands in order, so the ordinary single-floor case still
         *            visits the obvious surface before any alternative.
         */
        void StepCandidates(const NavTile& tile, int inTile, float fromZ,
                            const MoveProfile& profile, float climb,
                            std::vector<Surface>& out)
        {
            static thread_local std::vector<Surface> surfaces;
            tile.SurfacesAt(inTile, surfaces);

            out.clear();
            for (const Surface& s : surfaces)
            {
                if (s.layer >= MAX_SEARCH_LAYERS || !Admits(profile, s))
                {
                    continue;
                }
                if (std::fabs(s.z - fromZ) <= climb)
                {
                    out.push_back(s);
                }
            }

            std::sort(out.begin(), out.end(),
                      [fromZ](const Surface& a, const Surface& b)
                      {
                          return std::fabs(a.z - fromZ) < std::fabs(b.z - fromZ);
                      });
        }

        /// Is a local cell coordinate inside the tile?
        inline bool InTileBounds(int localX, int localY)
        {
            return localX >= 0 && localX < CELLS_PER_TILE && localY >= 0 &&
                   localY < CELLS_PER_TILE;
        }

        /**
         * @brief One step of the fine search.
         *
         * Diagonals refuse to cut corners: both orthogonal steps the diagonal is made of
         * must themselves be walkable. Without it a route slips diagonally between two
         * buildings through a gap of exactly zero yards -- which the client will not
         * follow, so the creature stops dead against the corner.
         */
        /// Every surface the step in `dir` could land on, with the corner rule applied.
        bool NeighbourCandidates(const NavTile& tile, int inTile, const Surface& from,
                                 int dir, const MoveProfile& profile, float climb,
                                 int& outCell, std::vector<Surface>& out)
        {
            out.clear();

            const int lx = LocalX(inTile) + DX[dir];
            const int ly = LocalY(inTile) + DY[dir];
            if (!InTileBounds(lx, ly))
            {
                return false;
            }

            outCell = InTileOf(lx, ly);
            StepCandidates(tile, outCell, from.z, profile, climb, out);
            if (out.empty())
            {
                return false;
            }

            if (dir >= 4)
            {
                Surface sideA;
                Surface sideB;
                const int cellA = InTileOf(LocalX(inTile) + DX[dir], LocalY(inTile));
                const int cellB = InTileOf(LocalX(inTile), LocalY(inTile) + DY[dir]);

                if (!StepTo(tile, cellA, from.z, profile, climb, sideA) ||
                    !StepTo(tile, cellB, from.z, profile, climb, sideB))
                {
                    out.clear();
                    return false;
                }
            }

            return true;
        }

        bool Neighbour(const NavTile& tile, int inTile, const Surface& from, int dir,
                       const MoveProfile& profile, float climb, int& outCell,
                       Surface& outSurface)
        {
            const int lx = LocalX(inTile) + DX[dir];
            const int ly = LocalY(inTile) + DY[dir];
            if (!InTileBounds(lx, ly))
            {
                return false;
            }

            outCell = InTileOf(lx, ly);
            if (!StepTo(tile, outCell, from.z, profile, climb, outSurface))
            {
                return false;
            }

            if (dir >= 4)
            {
                Surface sideA;
                Surface sideB;
                const int cellA = InTileOf(LocalX(inTile) + DX[dir], LocalY(inTile));
                const int cellB = InTileOf(LocalX(inTile), LocalY(inTile) + DY[dir]);

                if (!StepTo(tile, cellA, from.z, profile, climb, sideA) ||
                    !StepTo(tile, cellB, from.z, profile, climb, sideB))
                {
                    return false;
                }
            }

            return true;
        }

        /**
         * @brief Cross the border between two tiles: one step, no search.
         *
         * The cell the previous leg ended on lies against the border; its mirror is the
         * cell at the same position on the neighbour's facing side. What has to be
         * checked is only that the step is a step -- the heights within the climb, the
         * surface one this mover may stand on -- which is the same test every other step
         * of the route passed.
         */
        bool StepAcross(const NavTile& from, int fromCell, const Surface& fromSurface,
                        const NavTile& to, const MoveProfile& profile, int& outCell,
                        Surface& outSurface)
        {
            const uint8_t side = NavTile::SideTowards(to.TileX() - from.TileX(),
                                                      to.TileY() - from.TileY());
            if (side >= NavTile::SIDE_COUNT)
            {
                return false;
            }

            // The cell really has to be against that border. A leg that ended anywhere
            // else means the fine search stopped somewhere the corridor did not expect,
            // and stepping "across" from the middle of a tile would teleport the mover.
            if (NavTile::BorderCell(side, NavTile::BorderPosition(side, fromCell)) !=
                fromCell)
            {
                return false;
            }

            const int position = NavTile::BorderPosition(side, fromCell);
            const uint8_t farSide = NavTile::FacingSide(side);

            // The strictest of the three: neither bake's limit, nor the mover's, may be
            // exceeded. The store matched this border with the same rule.
            const float climb = std::min(
                std::min(from.Params().maxClimb, to.Params().maxClimb),
                profile.maxClimb);

            // Straight across first -- almost always the answer.
            if (StepTo(to, NavTile::BorderCell(farSide, position), fromSurface.z,
                       profile, climb, outSurface))
            {
                outCell = NavTile::BorderCell(farSide, position);
                return true;
            }

            // Then sideways, nearest first. A crossing is usable when ONE of its cells
            // is -- that is what its width means -- and the fine search stops at the
            // first cell of the gateway it happens to reach, which need not be that
            // one. Without this walk the coarse stage plans a route through a gate the
            // refinement then reports as a wall, and the mover falls back to a straight
            // line at a door it could have walked through.
            //
            // Bounded, because the search is meant to hand over near where it arrived:
            // past this the corridor was wrong and re-planning is the honest answer.
            constexpr int SEARCH_ALONG = 24;

            for (int step = 1; step <= SEARCH_ALONG; ++step)
            {
                for (int sign = -1; sign <= 1; sign += 2)
                {
                    const int at = position + sign * step;
                    if (at < 0 || at >= CELLS_PER_TILE)
                    {
                        continue;
                    }

                    // The step has to be legal on BOTH sides: walking along our own
                    // border to reach it, and then across. Only the second is tested
                    // here; the first is why the surface height carried in is the one
                    // the fine search ended on, and why the climb bounds it.
                    const int candidate = NavTile::BorderCell(farSide, at);
                    if (StepTo(to, candidate, fromSurface.z, profile, climb,
                               outSurface))
                    {
                        outCell = candidate;
                        return true;
                    }
                }
            }

            return false;
        }

        /// What a fine search is aiming at.
        struct FineGoal
        {
            /// A specific cell, when the leg ends at the caller's destination.
            int cell = -1;

            /// A gateway: any of its border cells will do, and taking the nearest is
            /// the whole reason this is not simply the gateway's midpoint. A route to
            /// the middle of a hundred-yard-wide crossing walks visibly out of its way.
            const Gateway* gateway = nullptr;

            /// Where the goal is, for the heuristic.
            Geometry::Vector3 aim;
        };

        bool AtGoal(const NavTile& tile, const FineGoal& goal, int inTile,
                    const Surface& surface)
        {
            if (goal.cell >= 0)
            {
                return inTile == goal.cell;
            }

            if (!goal.gateway || surface.region != goal.gateway->region)
            {
                return false;
            }

            const int lx = LocalX(inTile);
            const int ly = LocalY(inTile);
            int position = -1;

            switch (goal.gateway->side)
            {
                case NavTile::SIDE_LOW_X:
                    position = (lx == 0) ? ly : -1;
                    break;
                case NavTile::SIDE_HIGH_X:
                    position = (lx == CELLS_PER_TILE - 1) ? ly : -1;
                    break;
                case NavTile::SIDE_LOW_Y:
                    position = (ly == 0) ? lx : -1;
                    break;
                default:
                    position = (ly == CELLS_PER_TILE - 1) ? lx : -1;
                    break;
            }

            return position >= int(goal.gateway->first) &&
                   position <= int(goal.gateway->last);
        }

        struct FineEntry
        {
            float g = INF;
            uint32_t parent = 0;
            bool hasParent = false;
        };

        /**
         * @brief Search one tile's cells between two points.
         *
         * Bounded by `budget` expansions. Exhausting it is reported rather than
         * hidden: a search that stops at a wall and a search that stops because it ran
         * out of work call for opposite responses from the caller -- the first means
         * the goal is unreachable from here, the second means re-planning from further
         * along will make progress.
         *
         * @return True when the goal was reached. `path` is filled with in-tile cells
         *         and their surfaces, start first.
         */
        bool FineSearch(const NavTile& tile, int startCell, const Surface& startSurface,
                        const FineGoal& goal, const MoveProfile& profile,
                        uint32_t& budget,
                        std::vector<std::pair<int, Surface>>& path)
        {
            path.clear();

            // The BAKE's climb, or the mover's, whichever is stricter -- never a larger
            // one. The regions this search refines were flooded with the bake's limit,
            // so a query allowed to step further could walk between two cells the
            // coarse stage believes are in different regions and can only be joined
            // through a gateway. The two stages would then disagree about what is
            // connected, and the disagreement would show up only as the occasional
            // route that ignores a door.
            const float climb = std::min(tile.Params().maxClimb, profile.maxClimb);

            std::unordered_map<uint32_t, FineEntry> seen;
            seen.reserve(1024);

            std::unordered_map<uint32_t, Surface> surfaceOf;
            surfaceOf.reserve(1024);

            std::vector<Surface> candidates;

            using Open = std::pair<float, uint32_t>;
            std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;

            const uint32_t startKey = NodeKey(startCell, startSurface.layer);
            seen[startKey].g = 0.0f;
            surfaceOf[startKey] = startSurface;
            open.push({Dist2D(CellWorld(tile, startCell, startSurface.z), goal.aim),
                       startKey});

            uint32_t reachedKey = 0;
            bool reached = false;

            while (!open.empty() && budget > 0)
            {
                const Open top = open.top();
                open.pop();
                --budget;

                const uint32_t key = top.second;
                const int cell = KeyCell(key);
                const Surface here = surfaceOf[key];

                if (AtGoal(tile, goal, cell, here))
                {
                    reachedKey = key;
                    reached = true;
                    break;
                }

                const float g = seen[key].g;

                for (int dir = 0; dir < DIRS; ++dir)
                {
                    int nextCell = 0;
                    if (!NeighbourCandidates(tile, cell, here, dir, profile, climb,
                                             nextCell, candidates))
                    {
                        continue;
                    }

                    for (const Surface& next : candidates)
                    {
                        const uint32_t nextKey = NodeKey(nextCell, next.layer);
                        const float step =
                            StepLength(dir) * CellPenalty(profile, next.area);
                        const float tentative = g + step;

                        FineEntry& entry = seen[nextKey];
                        if (tentative >= entry.g)
                        {
                            continue;
                        }

                        entry.g = tentative;
                        entry.parent = key;
                        entry.hasParent = true;
                        surfaceOf[nextKey] = next;

                        const float h =
                            Dist2D(CellWorld(tile, nextCell, next.z), goal.aim);
                        open.push({tentative + h, nextKey});
                    }
                }
            }

            if (!reached)
            {
                return false;
            }

            for (uint32_t key = reachedKey;;)
            {
                path.push_back({KeyCell(key), surfaceOf[key]});

                const FineEntry& entry = seen[key];
                if (!entry.hasParent)
                {
                    break;
                }
                key = entry.parent;
            }

            std::reverse(path.begin(), path.end());
            return true;
        }

        // ------------------------------------------------------------- coarse ----

        struct CoarseEntry
        {
            float g = INF;
            GateRef parent;
            bool hasParent = false;
        };

        /// Both ends of a gateway, in world coordinates, for an admissible heuristic.
        void GatewayEnds(const NavTile& tile, const Gateway& gate,
                         Geometry::Vector3& a, Geometry::Vector3& b)
        {
            const bool varyY = gate.side == NavTile::SIDE_LOW_X ||
                               gate.side == NavTile::SIDE_HIGH_X;
            const int fixed = (gate.side == NavTile::SIDE_LOW_X ||
                               gate.side == NavTile::SIDE_LOW_Y)
                                  ? 0
                                  : CELLS_PER_TILE - 1;

            if (varyY)
            {
                const float x = CellCentre(GlobalCell(tile.TileX(), fixed));
                a = Geometry::Vector3(
                    x, CellCentre(GlobalCell(tile.TileY(), gate.first)), gate.firstZ);
                b = Geometry::Vector3(
                    x, CellCentre(GlobalCell(tile.TileY(), gate.last)), gate.lastZ);
            }
            else
            {
                const float y = CellCentre(GlobalCell(tile.TileY(), fixed));
                a = Geometry::Vector3(
                    CellCentre(GlobalCell(tile.TileX(), gate.first)), y, gate.firstZ);
                b = Geometry::Vector3(
                    CellCentre(GlobalCell(tile.TileX(), gate.last)), y, gate.lastZ);
            }
        }

        /// Distance from a point to a gateway, measured to the nearest part of it. An
        /// overestimate here does not break the search, but it does make it pick the
        /// wrong crossing of a wide border, which is exactly what shows up as a
        /// creature walking to the middle of a field to get through a gap at its edge.
        float DistToGateway(const NavTile& tile, const Gateway& gate,
                            const Geometry::Vector3& from)
        {
            Geometry::Vector3 a;
            Geometry::Vector3 b;
            GatewayEnds(tile, gate, a, b);

            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float lenSq = abx * abx + aby * aby;
            if (lenSq <= 0.0001f)
            {
                return Dist2D(from, a);
            }

            float t = ((from.x - a.x) * abx + (from.y - a.y) * aby) / lenSq;
            t = std::max(0.0f, std::min(1.0f, t));

            Geometry::Vector3 nearest(a.x + abx * t, a.y + aby * t, a.z);
            return Dist2D(from, nearest);
        }

        /// May this mover use a gateway at all, on the evidence the coarse stage has?
        bool UsableGateway(const NavTile& tile, const Gateway& gate,
                           const MoveProfile& profile)
        {
            if (profile.radius > 0.0f && gate.width < profile.radius)
            {
                return false;
            }

            const Region* region = tile.RegionByIndex(gate.region);
            if (!region)
            {
                return false;
            }
            if ((region->areas & profile.allowedAreas) == 0)
            {
                return false;
            }
            if (profile.radius > 0.0f && region->maxClearance < profile.radius)
            {
                return false;
            }

            return true;
        }
    }

    // ------------------------------------------------------------------ Router ----

    bool Router::CanWalkLine(const Geometry::Vector3& from, const Geometry::Vector3& to,
                             const MoveProfile& profile, float tolerance) const
    {
        CellRef fromCell;
        Surface fromSurface;
        if (!m_store.SurfaceAt(from.x, from.y, from.z, tolerance, fromCell,
                               fromSurface))
        {
            return false;
        }

        const CellRef toCell = CellAt(to.x, to.y);
        if (fromCell.TileX() != toCell.TileX() || fromCell.TileY() != toCell.TileY())
        {
            // Across a border this answers no rather than guessing. The caller routes,
            // which is the correct and only slightly more expensive response.
            return false;
        }

        const std::shared_ptr<const NavTile> tile = m_store.TileOf(fromCell);
        if (!tile)
        {
            return false;
        }

        const float climb = std::max(tile->Params().maxClimb, profile.maxClimb);

        // Walk the line one cell at a time, carrying the surface forward. Carrying it
        // is the point: a line that crosses a step of half a yard four times is fine,
        // and a line that crosses a single drop of ten is not, and only a walk that
        // remembers where it was standing can tell those apart.
        int cell = fromCell.InTile();
        Surface surface = fromSurface;

        const int targetCell = toCell.InTile();
        int guard = CELLS_PER_TILE * 2;

        while (cell != targetCell && guard-- > 0)
        {
            const int dx = LocalX(targetCell) - LocalX(cell);
            const int dy = LocalY(targetCell) - LocalY(cell);

            // Step towards the target, preferring the diagonal when both axes still
            // have ground to cover. Not a Bresenham line: the exact cells a line
            // crosses matter less than that every step taken is a step a mover could
            // take, and this walk only ever takes those.
            int dir = -1;
            if (dx != 0 && dy != 0)
            {
                dir = dx < 0 ? (dy < 0 ? 4 : 5) : (dy < 0 ? 6 : 7);
            }
            else if (dx != 0)
            {
                dir = dx < 0 ? 0 : 1;
            }
            else
            {
                dir = dy < 0 ? 2 : 3;
            }

            int nextCell = 0;
            Surface next;
            if (!Neighbour(*tile, cell, surface, dir, profile, climb, nextCell, next))
            {
                return false;
            }

            cell = nextCell;
            surface = next;
        }

        return cell == targetCell;
    }

    void Router::Find(const RouteRequest& request, Route& out) const
    {
        out.Clear();

        CellRef startCell;
        Surface startSurface;
        CellRef endCell;
        Surface endSurface;

        const bool haveStart =
            m_store.SurfaceAt(request.start.x, request.start.y, request.start.z,
                              request.seatTolerance, startCell, startSurface);
        const bool haveEnd =
            m_store.SurfaceAt(request.end.x, request.end.y, request.end.z,
                              request.seatTolerance, endCell, endSurface);

        if (!haveStart || !haveEnd)
        {
            // A fact and not a verdict. Whether a mover may cross ground the bake does
            // not describe depends on what it is -- a swimmer over deep water, a flier
            // over anything -- and on the terrain under it, neither of which this layer
            // is allowed to know. The caller decides.
            out.outcome = RouteOutcome::Unroutable;
            out.stop = RouteStop::OffMesh;
            return;
        }

        if (!Admits(request.profile, startSurface) ||
            !Admits(request.profile, endSurface))
        {
            out.outcome = RouteOutcome::Unroutable;
            out.stop = request.profile.Fits(startSurface.clearance) &&
                               request.profile.Fits(endSurface.clearance)
                           ? RouteStop::OffMesh
                           : RouteStop::TooNarrow;
            return;
        }

        const std::shared_ptr<const NavTile> startTile = m_store.TileOf(startCell);
        const std::shared_ptr<const NavTile> endTile = m_store.TileOf(endCell);
        if (!startTile || !endTile)
        {
            out.outcome = RouteOutcome::Unroutable;
            out.stop = RouteStop::OffMesh;
            return;
        }

        uint32_t budget = request.budget.cells;

        // The legs of the journey, as cell paths inside single tiles. One leg per
        // gateway hop, plus the two ends.
        std::vector<Leg> legs;

        const bool sameTile = startTile == endTile;
        const bool sameRegion = sameTile && startSurface.region == endSurface.region;

        bool complete = false;
        RouteStop stop = RouteStop::Wall;

        if (sameRegion)
        {
            FineGoal goal;
            goal.cell = endCell.InTile();
            goal.aim = CellWorld(*startTile, endCell.InTile(), endSurface.z);

            std::vector<std::pair<int, Surface>> path;
            complete = FineSearch(*startTile, startCell.InTile(), startSurface, goal,
                                  request.profile, budget, path);
            if (complete)
            {
                Leg leg;
                leg.tile = startTile;
                leg.cells = path;
                legs.push_back(std::move(leg));
            }
            else if (budget == 0)
            {
                stop = RouteStop::NodeBudget;
            }
        }
        else
        {
            std::vector<GateRef> corridor;
            if (!Coarse(startCell, startSurface, endCell, endSurface, request.profile,
                        corridor))
            {
                out.outcome = RouteOutcome::Unroutable;
                out.stop = RouteStop::Wall;
                return;
            }

            complete = Refine(corridor, startCell, startSurface, endCell, endSurface,
                              request.profile, budget, legs, stop);
        }

        if (legs.empty())
        {
            out.outcome = RouteOutcome::Unroutable;
            out.stop = complete ? RouteStop::Failed : stop;
            return;
        }

        Emit(legs, request, out);

        if (complete && out.points.size() <= request.budget.points)
        {
            out.outcome = RouteOutcome::Routed;
            out.stop = RouteStop::Reached;
        }
        else
        {
            out.outcome = RouteOutcome::Partial;
            out.stop = out.points.size() > request.budget.points
                           ? RouteStop::PointBudget
                           : stop;
        }
    }

    // ------------------------------------------------------------------ coarse ----

    bool Router::Coarse(const CellRef& startCell, const Surface& startSurface,
                        const CellRef& endCell, const Surface& endSurface,
                        const MoveProfile& profile,
                        std::vector<GateRef>& corridor) const
    {
        corridor.clear();

        const std::shared_ptr<const NavTile> startTile = m_store.TileOf(startCell);
        const std::shared_ptr<const NavTile> endTile = m_store.TileOf(endCell);
        if (!startTile || !endTile)
        {
            return false;
        }

        const Geometry::Vector3 from =
            CellWorld(*startTile, startCell.InTile(), startSurface.z);
        const Geometry::Vector3 to =
            CellWorld(*endTile, endCell.InTile(), endSurface.z);

        std::unordered_map<uint64_t, CoarseEntry> seen;
        using Open = std::pair<float, uint64_t>;
        std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;

        // Seed with every gateway of the start tile that leads out of the region the
        // mover is standing in. The distance to each is a straight line, and only for
        // these first and last hops: everything between is a measured cost.
        for (size_t i = 0; i < startTile->Gateways().size(); ++i)
        {
            const Gateway& gate = startTile->Gateways()[i];
            if (gate.region != startSurface.region ||
                !UsableGateway(*startTile, gate, profile))
            {
                continue;
            }

            GateRef ref;
            ref.tileX = int16_t(startTile->TileX());
            ref.tileY = int16_t(startTile->TileY());
            ref.gate = uint16_t(i);

            const uint64_t key = PackGate(ref);
            const float g = DistToGateway(*startTile, gate, from);

            seen[key].g = g;
            open.push({g + DistToGateway(*startTile, gate, to), key});
        }

        uint64_t reached = 0;
        bool found = false;

        while (!open.empty())
        {
            const Open top = open.top();
            open.pop();

            const uint64_t key = top.second;
            const GateRef here = UnpackGate(key);
            const std::shared_ptr<const NavTile> tile =
                m_store.TileAt(here.tileX, here.tileY);
            if (!tile || here.gate >= tile->Gateways().size())
            {
                continue;
            }

            const float g = seen[key].g;
            if (top.first > g + DistToGateway(*tile, tile->Gateways()[here.gate], to) +
                                0.001f)
            {
                continue;   // a stale queue entry, superseded by a cheaper route here
            }

            // Arrived: this gateway opens onto the region the destination sits in, so
            // the last leg is a walk inside this tile and needs no more gateways.
            if (here.tileX == int16_t(endTile->TileX()) &&
                here.tileY == int16_t(endTile->TileY()) &&
                tile->Gateways()[here.gate].region == endSurface.region)
            {
                reached = key;
                found = true;
                break;
            }

            // Inside the tile: what the baker measured between this gateway and each
            // of the others. No estimate, no straight line.
            for (size_t i = 0; i < tile->Gateways().size(); ++i)
            {
                if (uint16_t(i) == here.gate)
                {
                    continue;
                }

                const float cost = tile->GatewayCost(here.gate, i);
                if (cost >= NavTile::UNREACHABLE ||
                    !UsableGateway(*tile, tile->Gateways()[i], profile))
                {
                    continue;
                }

                GateRef next = here;
                next.gate = uint16_t(i);
                const uint64_t nextKey = PackGate(next);

                CoarseEntry& entry = seen[nextKey];
                if (g + cost >= entry.g)
                {
                    continue;
                }

                entry.g = g + cost;
                entry.parent = here;
                entry.hasParent = true;
                open.push({entry.g + DistToGateway(*tile, tile->Gateways()[i], to),
                           nextKey});
            }

            // Across the border: the joins the store computed when both tiles arrived.
            for (const Crossing& crossing : m_store.CrossingsOf(here))
            {
                if (profile.radius > 0.0f && crossing.width < profile.radius)
                {
                    continue;
                }

                const std::shared_ptr<const NavTile> farTile =
                    m_store.TileAt(crossing.to.tileX, crossing.to.tileY);
                if (!farTile || crossing.to.gate >= farTile->Gateways().size() ||
                    !UsableGateway(*farTile, farTile->Gateways()[crossing.to.gate],
                                   profile))
                {
                    continue;
                }

                const uint64_t nextKey = PackGate(crossing.to);
                CoarseEntry& entry = seen[nextKey];
                if (g + crossing.cost >= entry.g)
                {
                    continue;
                }

                entry.g = g + crossing.cost;
                entry.parent = here;
                entry.hasParent = true;
                open.push({entry.g + DistToGateway(
                                         *farTile,
                                         farTile->Gateways()[crossing.to.gate], to),
                           nextKey});
            }
        }

        if (!found)
        {
            return false;
        }

        for (uint64_t key = reached;;)
        {
            corridor.push_back(UnpackGate(key));

            const CoarseEntry& entry = seen[key];
            if (!entry.hasParent)
            {
                break;
            }
            key = PackGate(entry.parent);
        }

        std::reverse(corridor.begin(), corridor.end());
        return true;
    }

    // ------------------------------------------------------------------ refine ----

    bool Router::Refine(const std::vector<GateRef>& corridor, const CellRef& startCell,
                        const Surface& startSurface, const CellRef& endCell,
                        const Surface& endSurface, const MoveProfile& profile,
                        uint32_t& budget, std::vector<Leg>& legs,
                        RouteStop& stop) const
    {
        legs.clear();
        stop = RouteStop::Wall;

        std::shared_ptr<const NavTile> tile = m_store.TileOf(startCell);
        if (!tile)
        {
            return false;
        }

        int cell = startCell.InTile();
        Surface surface = startSurface;

        std::vector<std::pair<int, Surface>> path;

        for (size_t i = 0; i < corridor.size(); ++i)
        {
            const GateRef& ref = corridor[i];
            const std::shared_ptr<const NavTile> target =
                m_store.TileAt(ref.tileX, ref.tileY);
            if (!target || ref.gate >= target->Gateways().size())
            {
                return false;
            }

            if (target != tile)
            {
                // A crossing: the previous leg ended on this tile's border and the next
                // cell is its mirror in the neighbour. One step, no search.
                if (!StepAcross(*tile, cell, surface, *target, profile, cell, surface))
                {
                    return false;
                }

                tile = target;

                Leg leg;
                leg.tile = tile;
                leg.cells.push_back({cell, surface});
                legs.push_back(std::move(leg));
                continue;
            }

            FineGoal goal;
            goal.gateway = &target->Gateways()[ref.gate];
            goal.aim = Geometry::Vector3(goal.gateway->x, goal.gateway->y,
                                         goal.gateway->z);

            if (!FineSearch(*tile, cell, surface, goal, profile, budget, path))
            {
                stop = (budget == 0) ? RouteStop::NodeBudget : RouteStop::Wall;
                return false;
            }

            Leg leg;
            leg.tile = tile;
            leg.cells = path;
            legs.push_back(std::move(leg));

            cell = path.back().first;
            surface = path.back().second;
        }

        // The last stretch: from wherever the final gateway left us, to the goal.
        const std::shared_ptr<const NavTile> endTile = m_store.TileOf(endCell);
        if (!endTile)
        {
            return false;
        }

        if (endTile != tile)
        {
            if (!StepAcross(*tile, cell, surface, *endTile, profile, cell, surface))
            {
                return false;
            }
            tile = endTile;
        }

        FineGoal goal;
        goal.cell = endCell.InTile();
        goal.aim = CellWorld(*endTile, endCell.InTile(), endSurface.z);

        if (!FineSearch(*tile, cell, surface, goal, profile, budget, path))
        {
            stop = (budget == 0) ? RouteStop::NodeBudget : RouteStop::Wall;
            return false;
        }

        Leg leg;
        leg.tile = tile;
        leg.cells = path;
        legs.push_back(std::move(leg));

        return true;
    }

    // -------------------------------------------------------------------- emit ----

    void Router::Emit(const std::vector<Leg>& legs, const RouteRequest& request,
                      Route& out) const
    {
        // Flatten, keeping only where the walk TURNS. A grid path of seven hundred
        // cells across a tile is a straight line with two bends in it; carrying all
        // seven hundred into the line-of-walk pull below would make an O(n^2) step out
        // of what is otherwise a handful of tests.
        struct Corner
        {
            Geometry::Vector3 pos;
            uint8_t area = 0;
        };

        std::vector<Corner> corners;
        int lastDx = 0;
        int lastDy = 0;
        bool havePrev = false;
        int prevCell = 0;
        const NavTile* prevTile = nullptr;

        for (const Leg& leg : legs)
        {
            for (const std::pair<int, Surface>& step : leg.cells)
            {
                const Corner corner{CellWorld(*leg.tile, step.first, step.second.z),
                                    step.second.area};

                if (!havePrev || leg.tile.get() != prevTile)
                {
                    corners.push_back(corner);
                    havePrev = true;
                    lastDx = 0;
                    lastDy = 0;
                }
                else
                {
                    const int dx = LocalX(step.first) - LocalX(prevCell);
                    const int dy = LocalY(step.first) - LocalY(prevCell);
                    if (dx != lastDx || dy != lastDy)
                    {
                        corners.push_back(corner);
                        lastDx = dx;
                        lastDy = dy;
                    }
                    else
                    {
                        corners.back() = corner;
                    }
                }

                prevCell = step.first;
                prevTile = leg.tile.get();
            }
        }

        if (corners.empty())
        {
            return;
        }

        // Now drop the corners a straight walk makes unnecessary. Proved against the
        // same cells the route was found on, so a point is only removed when the mover
        // really can walk past where it was.
        std::vector<Corner> pulled;
        pulled.push_back(corners.front());

        size_t at = 0;
        while (at + 1 < corners.size())
        {
            size_t next = at + 1;
            for (size_t probe = corners.size() - 1; probe > at + 1; --probe)
            {
                if (CanWalkLine(corners[at].pos, corners[probe].pos, request.profile,
                                request.seatTolerance))
                {
                    next = probe;
                    break;
                }
            }

            pulled.push_back(corners[next]);
            at = next;
        }

        out.points.reserve(pulled.size() + 1);
        out.points.push_back(request.start);

        bool truncated = false;
        float walked = 0.0f;

        for (size_t i = 0; i < pulled.size(); ++i)
        {
            if (out.points.size() >= request.budget.points)
            {
                truncated = true;
                break;
            }

            const bool swimming = AreaOf(pulled[i].area) == NavArea::Water;
            Geometry::Vector3 point = pulled[i].pos;
            point.z += swimming ? -SWIM_SEAT_DEPTH : GROUND_CLEARANCE;

            // The length cap, measured in yards over the points actually emitted --
            // which is the only place it CAN be measured, now that a point stands for a
            // corner rather than for a fixed four yards of walking.
            if (request.budget.maxLength > 0.0f)
            {
                const float leg = Dist2D(out.points.back(), point);
                if (walked + leg > request.budget.maxLength)
                {
                    truncated = true;
                    break;
                }
                walked += leg;
            }

            out.points.push_back(point);
        }

        // Only when the whole route fitted. Moving the last point to the destination
        // after the budget has CUT the route replaces a walked corner with a straight
        // line through everything the search never visited -- a pet whose follow hits
        // the ceiling would jump from wherever it got to, into its owner, through a
        // building. That is exactly the fallback this router refuses to report as
        // success, so it must not smuggle one in at the last step.
        //
        // Left as Partial instead, and the caller re-plans -- which a follow does every
        // fifty milliseconds anyway.
        if (request.forceDestination && !truncated && !out.points.empty())
        {
            out.points.back() = request.end;
        }
    }
}
