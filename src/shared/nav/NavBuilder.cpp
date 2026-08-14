#include "nav/NavBuilder.hpp"

#include "terrain/Column.hpp"
#include "terrain/FusedTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace Nav
{
    namespace
    {
        using world::terrain::Column;
        using world::terrain::LiquidKind;
        using world::terrain::SurfaceKind;

        constexpr float INF = std::numeric_limits<float>::max();

        /// The eight directions a node may connect in, orthogonals first so a search
        /// that stops early has taken the cheap steps.
        constexpr int DIRS = 8;
        constexpr int DX[DIRS] = {-1, 1, 0, 0, -1, -1, 1, 1};
        constexpr int DY[DIRS] = {0, 0, -1, 1, -1, 1, -1, 1};

        /// The direction that undoes each one, so a link can be made to point both ways.
        constexpr int OPP[DIRS] = {1, 0, 3, 2, 7, 6, 5, 4};

        /// Step length in yards for each direction.
        inline float StepLength(int dir)
        {
            return dir < 4 ? CELL_SIZE : CELL_SIZE * 1.41421356f;
        }

        /// A walkable surface found while sampling, before it is numbered or placed.
        struct Cand
        {
            float z = 0.0f;
            NavArea area = NavArea::Blocked;
            uint8_t flags = 0;
            float clearance = 0.0f;
            int32_t region = -1;
        };

        /**
         * @brief The padded working grid for one tile.
         *
         * Padded because everything measured from neighbours -- slope, room, whether a
         * step is a step -- is wrong at an edge that has no neighbours. The margin is
         * sampled like any other cell and thrown away at the end, so the tile's own
         * border cells were computed with real ground on both sides.
         */
        struct Work
        {
            int margin = 0;
            int side = 0;          ///< padded cells along one edge
            int tileX = 0;
            int tileY = 0;

            std::vector<Cand> nodes;
            std::vector<uint32_t> first;   ///< size cells+1; nodes of cell c are
                                           ///< [first[c], first[c+1])
            std::vector<int32_t> link;     ///< DIRS entries per node, -1 when none

            /// Which padded cell each node belongs to. The flood and the gateway scan
            /// both have to ask "is this node one of the tile's own", and walking back
            /// from a node index to a cell is otherwise a binary search over `first`.
            std::vector<int32_t> nodeCell;

            int Cells() const { return side * side; }
            int CellAt(int px, int py) const { return px * side + py; }

            bool InPad(int px, int py) const
            {
                return px >= 0 && px < side && py >= 0 && py < side;
            }

            /// True when a padded coordinate is one of the tile's own cells.
            bool InTile(int px, int py) const
            {
                return px >= margin && px < margin + CELLS_PER_TILE &&
                       py >= margin && py < margin + CELLS_PER_TILE;
            }

            uint32_t NodeBegin(int cell) const { return first[size_t(cell)]; }
            uint32_t NodeEnd(int cell) const { return first[size_t(cell) + 1]; }

            int32_t Neighbour(uint32_t node, int dir) const
            {
                return link[size_t(node) * DIRS + size_t(dir)];
            }
        };

        NavArea AreaOfLiquid(LiquidKind kind)
        {
            switch (kind)
            {
                case LiquidKind::Magma: return NavArea::Magma;
                case LiquidKind::Slime: return NavArea::Slime;
                default: return NavArea::Water;
            }
        }

        /**
         * @brief Working buffers for one cell's gather, reused across all of them.
         *
         * A tile is a quarter of a million cells and this ran six vectors deep, so
         * building them per cell was a million and a half allocations per tile -- more
         * time than the ray queries the gather exists to make.
         */
        struct GatherScratch
        {
            std::vector<float> solids;
            std::vector<SurfaceKind> solidKind;
            std::vector<const world::terrain::Surface*> liquids;
            std::vector<size_t> order;
            std::vector<float> z;
            std::vector<SurfaceKind> kind;
        };

        /**
         * @brief Every surface a mover could stand on or swim in at one point.
         *
         * The whole of the geometry stage, and it is one function because it is one
         * question. A floor qualifies when there is room to stand on it; what covers it
         * decides what it is made of; and a body of water deep enough to swim in is a
         * surface in its own right, above the seabed rather than instead of it -- a crab
         * crosses the bay along the bottom while a fish crosses it over the top, and
         * both are true at once.
         */
        void GatherCell(const Column& column, const BuildParams& params,
                        GatherScratch& scratch, std::vector<Cand>& out)
        {
            out.clear();

            std::vector<float>& solids = scratch.solids;
            std::vector<SurfaceKind>& solidKind = scratch.solidKind;
            std::vector<const world::terrain::Surface*>& liquids = scratch.liquids;

            solids.clear();
            solidKind.clear();
            liquids.clear();

            for (const world::terrain::Surface& s : column.Surfaces())
            {
                if (s.Solid())
                {
                    solids.push_back(s.z);
                    solidKind.push_back(s.kind);
                }
                else
                {
                    liquids.push_back(&s);
                }
            }

            // Sort the solids together with their kind. Two small parallel arrays and an
            // index sort, rather than a vector of pairs, because the kind is read once.
            std::vector<size_t>& order = scratch.order;
            order.resize(solids.size());
            for (size_t i = 0; i < order.size(); ++i)
            {
                order[i] = i;
            }
            std::sort(order.begin(), order.end(),
                      [&solids](size_t a, size_t b) { return solids[a] < solids[b]; });

            std::vector<float>& z = scratch.z;
            std::vector<SurfaceKind>& kind = scratch.kind;
            z.resize(solids.size());
            kind.resize(solids.size());
            for (size_t i = 0; i < order.size(); ++i)
            {
                z[i] = solids[order[i]];
                kind[i] = solidKind[order[i]];
            }

            for (size_t i = 0; i < z.size(); ++i)
            {
                const float floorZ = z[i];
                const float ceiling = (i + 1 < z.size()) ? z[i + 1] : INF;

                // A floor with a ceiling too low to stand under is not a place a mover
                // can be, however solid it is. This is the single test that keeps
                // routes out of the gap between a building's storeys.
                if (ceiling - floorZ < params.agentHeight)
                {
                    continue;
                }

                Cand cand;
                cand.z = floorZ;
                cand.area = NavArea::Ground;
                cand.flags = (kind[i] == SurfaceKind::Terrain) ? 0 : CELL_MODEL;

                // The liquid standing ON this floor: the highest one above it that no
                // other floor separates from it. The solids are sorted, so "no floor
                // between" is exactly "below the ceiling".
                const world::terrain::Surface* cover = nullptr;
                for (const world::terrain::Surface* l : liquids)
                {
                    if (l->z > floorZ && l->z < ceiling &&
                        (!cover || l->z > cover->z))
                    {
                        cover = l;
                    }
                }

                if (cover)
                {
                    const NavArea liquidArea = AreaOfLiquid(cover->liquid);
                    if (liquidArea == NavArea::Magma || liquidArea == NavArea::Slime)
                    {
                        // Nothing wades through lava, and nothing walks under it
                        // either. The floor takes the liquid's area so that no profile
                        // admitting ground admits this.
                        cand.area = liquidArea;
                    }
                    else if (cover->z - floorZ <= params.swimDepth)
                    {
                        cand.area = NavArea::Shallow;
                    }
                    else
                    {
                        // Deep. The floor stays a floor -- it is the seabed, and a
                        // walker keeps his feet on it -- and the surface above becomes
                        // a second, separate place to be.
                        Cand swim;
                        swim.z = cover->z;
                        swim.area = NavArea::Water;
                        swim.flags = 0;
                        out.push_back(swim);
                    }
                }

                out.push_back(cand);
            }

            // Open water with no floor inside the sampling window -- the deep ocean --
            // still has a surface to swim on. Without this the sea is a hole in the
            // navigation and every route across a bay falls back to a straight line.
            for (const world::terrain::Surface* l : liquids)
            {
                const NavArea area = AreaOfLiquid(l->liquid);
                if (area != NavArea::Water)
                {
                    continue;
                }

                bool have = false;
                for (const Cand& c : out)
                {
                    if (std::fabs(c.z - l->z) < 0.5f)
                    {
                        have = true;
                        break;
                    }
                }

                if (!have)
                {
                    Cand swim;
                    swim.z = l->z;
                    swim.area = NavArea::Water;
                    out.push_back(swim);
                }
            }

            std::sort(out.begin(), out.end(),
                      [](const Cand& a, const Cand& b) { return a.z < b.z; });
        }

        /// Sample the padded grid.
        void Sample(const world::terrain::FusedTerrain& terrain,
                    const BuildParams& params, Work& work)
        {
            work.first.assign(size_t(work.Cells()) + 1, 0);
            work.nodes.clear();
            work.nodeCell.clear();
            work.nodes.reserve(size_t(work.Cells()));
            work.nodeCell.reserve(size_t(work.Cells()));

            Column column;
            std::vector<Cand> cands;
            GatherScratch scratch;

            for (int px = 0; px < work.side; ++px)
            {
                const int cellX = GlobalCell(work.tileX, px - work.margin);

                for (int py = 0; py < work.side; ++py)
                {
                    const int cell = work.CellAt(px, py);
                    work.first[size_t(cell)] = uint32_t(work.nodes.size());

                    const int cellY = GlobalCell(work.tileY, py - work.margin);
                    if (!OnMap(cellX) || !OnMap(cellY))
                    {
                        continue;
                    }

                    const float wx = CellCentre(cellX);
                    const float wy = CellCentre(cellY);

                    column = terrain.ColumnAt(wx, wy, params.zTop, params.zBottom);
                    GatherCell(column, params, scratch, cands);

                    for (const Cand& c : cands)
                    {
                        work.nodes.push_back(c);
                        work.nodeCell.push_back(cell);
                    }
                }
            }

            work.first[size_t(work.Cells())] = uint32_t(work.nodes.size());
        }

        /**
         * @brief The surface of a neighbouring cell a mover would step onto, if any.
         *
         * The nearest by height within the climb limit. "Nearest" and not "first"
         * matters where floors stack: stepping off a balcony, the cell in front holds
         * both the balcony's continuation and the ground two storeys down, and only one
         * of them is a step.
         */
        int32_t StepTo(const Work& work, int cell, float fromZ, float maxClimb)
        {
            int32_t best = -1;
            float bestDelta = maxClimb;

            for (uint32_t n = work.NodeBegin(cell); n < work.NodeEnd(cell); ++n)
            {
                const float delta = std::fabs(work.nodes[n].z - fromZ);
                if (delta <= bestDelta)
                {
                    bestDelta = delta;
                    best = int32_t(n);
                }
            }

            return best;
        }

        /**
         * @brief Wire every node to the nodes a mover can step to.
         *
         * Built once and kept, because three separate passes walk it: the room
         * measurement, the region flood, and the gateway distances. Recomputing a
         * neighbour costs a scan of the target cell's surfaces, and doing that three
         * times over a quarter of a million nodes is the difference between a bake that
         * takes a second per tile and one that takes four.
         *
         * Diagonals refuse to cut corners: both orthogonal steps that make up the
         * diagonal must themselves be walkable. Without it a route slips through the
         * join between two buildings, which is a gap of exactly zero yards.
         */
        void Connect(Work& work, const BuildParams& params)
        {
            work.link.assign(work.nodes.size() * DIRS, -1);

            for (int px = 0; px < work.side; ++px)
            {
                for (int py = 0; py < work.side; ++py)
                {
                    const int cell = work.CellAt(px, py);

                    for (uint32_t n = work.NodeBegin(cell); n < work.NodeEnd(cell); ++n)
                    {
                        const float z = work.nodes[n].z;

                        for (int dir = 0; dir < DIRS; ++dir)
                        {
                            const int nx = px + DX[dir];
                            const int ny = py + DY[dir];
                            if (!work.InPad(nx, ny))
                            {
                                continue;
                            }

                            const int32_t to = StepTo(work, work.CellAt(nx, ny), z,
                                                      params.maxClimb);
                            if (to < 0)
                            {
                                continue;
                            }

                            if (dir >= 4)
                            {
                                // The two orthogonal steps this diagonal is made of.
                                const int32_t sideA =
                                    StepTo(work, work.CellAt(px + DX[dir], py), z,
                                           params.maxClimb);
                                const int32_t sideB =
                                    StepTo(work, work.CellAt(px, py + DY[dir]), z,
                                           params.maxClimb);
                                if (sideA < 0 || sideB < 0)
                                {
                                    continue;
                                }
                            }

                            work.link[size_t(n) * DIRS + size_t(dir)] = to;
                        }
                    }
                }
            }

            // Make the graph undirected. StepTo picks the NEAREST surface by height,
            // and nearest is not a symmetric relation where floors stack: standing on a
            // balcony the nearest surface in front may be the balcony's continuation,
            // while from that continuation the nearest surface back may be the ground
            // below. Everything downstream -- the room measurement, the flood, the
            // gateway distances -- assumes a step that exists one way exists the other,
            // so the weaker direction is filled in rather than left to be discovered.
            for (size_t n = 0; n < work.nodes.size(); ++n)
            {
                for (int dir = 0; dir < DIRS; ++dir)
                {
                    const int32_t to = work.link[n * DIRS + size_t(dir)];
                    if (to < 0)
                    {
                        continue;
                    }

                    int32_t& back = work.link[size_t(to) * DIRS + size_t(OPP[dir])];
                    if (back < 0)
                    {
                        back = int32_t(n);
                    }
                }
            }
        }

        /**
         * @brief Drop what is too steep to stand on, and mark what is merely steep.
         *
         * The gradient is measured only against neighbours a mover could actually STEP
         * to. Measuring against every adjacent cell would read the wall beside a flat
         * ledge as a vertical slope and delete the ledge -- the ground next to a cliff
         * is not itself a cliff.
         */
        void Slope(Work& work, const BuildParams& params)
        {
            const float maxTan = std::tan(params.maxSlopeDeg * 3.14159265f / 180.0f);
            const float steepTan = std::tan(params.steepSlopeDeg * 3.14159265f / 180.0f);

            std::vector<float> gradient(work.nodes.size(), 0.0f);

            for (size_t n = 0; n < work.nodes.size(); ++n)
            {
                const Cand& cand = work.nodes[n];
                if (cand.area == NavArea::Water)
                {
                    continue;   // a liquid surface is level by definition
                }

                float slope = 0.0f;
                for (int dir = 0; dir < 4; ++dir)
                {
                    const int32_t to = work.Neighbour(uint32_t(n), dir);
                    if (to < 0)
                    {
                        continue;
                    }

                    const float rise = std::fabs(work.nodes[size_t(to)].z - cand.z);
                    slope = std::max(slope, rise / CELL_SIZE);
                }

                gradient[n] = slope;
            }

            for (size_t n = 0; n < work.nodes.size(); ++n)
            {
                if (gradient[n] > maxTan)
                {
                    work.nodes[n].area = NavArea::Blocked;
                }
                else if (gradient[n] > steepTan)
                {
                    work.nodes[n].flags |= CELL_STEEP;
                }
            }

            // Blocking a node invalidates every link that pointed at it, in both
            // directions. Leaving them would let a route step onto ground the slope
            // pass just refused.
            for (size_t n = 0; n < work.nodes.size(); ++n)
            {
                const bool dead = work.nodes[n].area == NavArea::Blocked;
                for (int dir = 0; dir < DIRS; ++dir)
                {
                    int32_t& to = work.link[n * DIRS + size_t(dir)];
                    if (to < 0)
                    {
                        continue;
                    }
                    if (dead || work.nodes[size_t(to)].area == NavArea::Blocked)
                    {
                        to = -1;
                    }
                }
            }
        }

        /**
         * @brief How much room each node has, in yards.
         *
         * A multi-source shortest path outwards from every node that touches an edge --
         * a wall, a drop, a slope too steep. The result is the distance to the nearest
         * place a mover cannot be, which is exactly what a width test wants, and it is
         * measured over the CONNECTIVITY rather than over the plan, so the corridor
         * inside a building is not credited with the open field on the other side of
         * its wall.
         *
         * This is what replaces eroding the whole surface by one agent radius at bake
         * time. Recording the room and comparing it at query time is strictly more
         * information for the same pass, and it is what lets one bake serve a murloc
         * and a devilsaur.
         */
        void Clearance(Work& work)
        {
            const size_t count = work.nodes.size();
            std::vector<float> dist(count, INF);

            using Entry = std::pair<float, uint32_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

            for (size_t n = 0; n < count; ++n)
            {
                if (work.nodes[n].area == NavArea::Blocked)
                {
                    continue;
                }

                bool edge = false;
                for (int dir = 0; dir < DIRS && !edge; ++dir)
                {
                    edge = work.Neighbour(uint32_t(n), dir) < 0;
                }

                if (edge)
                {
                    // Half a cell: the wall is at the cell's rim, not at its centre.
                    dist[n] = CELL_SIZE * 0.5f;
                    open.push({dist[n], uint32_t(n)});

                    // Recorded as well as used. The router prices these a little above
                    // open ground, so a creature crossing a bridge walks down the middle
                    // of it rather than along the parapet.
                    work.nodes[n].flags |= CELL_BORDER;
                }
            }

            while (!open.empty())
            {
                const Entry top = open.top();
                open.pop();

                if (top.first > dist[top.second])
                {
                    continue;
                }

                for (int dir = 0; dir < DIRS; ++dir)
                {
                    const int32_t to = work.Neighbour(top.second, dir);
                    if (to < 0)
                    {
                        continue;
                    }

                    const float next = top.first + StepLength(dir);
                    if (next < dist[size_t(to)])
                    {
                        dist[size_t(to)] = next;
                        open.push({next, uint32_t(to)});
                    }
                }
            }

            for (size_t n = 0; n < count; ++n)
            {
                work.nodes[n].clearance = (dist[n] == INF) ? 255.0f * CLEARANCE_QUANTUM
                                                           : dist[n];
            }
        }

        /// Is a node one of the tile's own, rather than one of the margin's?
        inline bool OwnNode(const Work& work, int32_t node)
        {
            const int cell = work.nodeCell[size_t(node)];
            return work.InTile(cell / work.side, cell % work.side);
        }

        /**
         * @brief Regions smaller than this are dropped, in nodes.
         *
         * A handful of cells nothing can reach except by falling onto them is not a
         * place to route through, and every one of them would otherwise take a row and
         * a column in the gateway cost matrix. The mover that finds itself standing on
         * one is not stranded: the router answers "not on the mesh" for it, which is
         * the case the direct fallback exists for.
         */
        constexpr uint32_t MIN_REGION_NODES = 6;

        /**
         * @brief Number the connected components of the tile's own cells.
         *
         * The margin is deliberately excluded, in both the seeding and the flood. A
         * region means "reachable without leaving this tile", and a flood that ran
         * through the margin would join two regions that only meet in the neighbour --
         * which is precisely the fact the coarse search needs to be TOLD, by a gateway,
         * rather than to have hidden inside a region.
         *
         * @return The number of regions, after the small ones have been dropped.
         */
        uint32_t Regionise(Work& work)
        {
            std::vector<uint32_t> size;
            std::vector<uint32_t> stack;
            uint32_t next = 0;

            for (int px = work.margin; px < work.margin + CELLS_PER_TILE; ++px)
            {
                for (int py = work.margin; py < work.margin + CELLS_PER_TILE; ++py)
                {
                    const int cell = work.CellAt(px, py);

                    for (uint32_t n = work.NodeBegin(cell); n < work.NodeEnd(cell); ++n)
                    {
                        if (work.nodes[n].region >= 0 ||
                            work.nodes[n].area == NavArea::Blocked)
                        {
                            continue;
                        }

                        const int32_t id = int32_t(next++);
                        uint32_t count = 0;

                        work.nodes[n].region = id;
                        stack.push_back(n);

                        while (!stack.empty())
                        {
                            const uint32_t at = stack.back();
                            stack.pop_back();
                            ++count;

                            for (int dir = 0; dir < DIRS; ++dir)
                            {
                                const int32_t to = work.Neighbour(at, dir);
                                if (to < 0 || work.nodes[size_t(to)].region >= 0 ||
                                    !OwnNode(work, to))
                                {
                                    continue;
                                }

                                work.nodes[size_t(to)].region = id;
                                stack.push_back(uint32_t(to));
                            }
                        }

                        size.push_back(count);
                    }
                }
            }

            // Renumber, dropping the specks. Done as a second pass over a remap table
            // rather than by deleting as we go, because a region's size is only known
            // once its flood has finished.
            std::vector<int32_t> remap(size.size(), -1);
            uint32_t kept = 0;
            for (size_t i = 0; i < size.size(); ++i)
            {
                if (size[i] >= MIN_REGION_NODES && kept < 0xFFFFu)
                {
                    remap[i] = int32_t(kept++);
                }
            }

            for (Cand& node : work.nodes)
            {
                if (node.region < 0)
                {
                    continue;
                }

                node.region = remap[size_t(node.region)];
                if (node.region < 0)
                {
                    node.area = NavArea::Blocked;
                }
            }

            return kept;
        }

        /// One run of border cells, while it is still being grown.
        struct Run
        {
            uint32_t last = 0;      ///< the node at the run's current end
            uint16_t first = 0;     ///< position along the border where it started
            uint16_t at = 0;        ///< position it currently reaches
            int32_t region = -1;
            float firstZ = 0.0f;
            float width = 0.0f;
            bool alive = false;
        };

        /// Where along the border a position sits, as a padded cell coordinate.
        void BorderCell(const Work& work, uint8_t side, int t, int& px, int& py)
        {
            const int lo = work.margin;
            const int hi = work.margin + CELLS_PER_TILE - 1;

            switch (side)
            {
                case NavTile::SIDE_LOW_X:  px = lo;     py = lo + t; break;
                case NavTile::SIDE_HIGH_X: px = hi;     py = lo + t; break;
                case NavTile::SIDE_LOW_Y:  px = lo + t; py = lo;     break;
                default:                   px = lo + t; py = hi;     break;
            }
        }

        /// The direction that walks ALONG a border, so a run can test that its cells
        /// are actually connected to each other rather than merely adjacent.
        int AlongDir(uint8_t side)
        {
            // Sides 0 and 1 vary in y, sides 2 and 3 vary in x.
            return (side == NavTile::SIDE_LOW_X || side == NavTile::SIDE_HIGH_X) ? 3 : 1;
        }

        /**
         * @brief Find the tile's ways in and out.
         *
         * A gateway is a maximal run of border cells of one region, each connected to
         * the next along the border. Maximal matters: an open hillside crossing a tile
         * boundary is ONE gateway a hundred yards wide, not a hundred and fifty
         * separate ones, and that is the difference between a coarse graph with tens of
         * nodes and one with tens of thousands.
         */
        void FindGateways(const Work& work, std::vector<Gateway>& out,
                          std::vector<std::vector<uint32_t>>& members)
        {
            out.clear();
            members.clear();

            for (uint8_t side = 0; side < NavTile::SIDE_COUNT; ++side)
            {
                const int along = AlongDir(side);
                std::vector<Run> runs;
                std::vector<std::vector<uint32_t>> runNodes;

                for (int t = 0; t < CELLS_PER_TILE; ++t)
                {
                    int px = 0;
                    int py = 0;
                    BorderCell(work, side, t, px, py);
                    const int cell = work.CellAt(px, py);

                    for (Run& run : runs)
                    {
                        run.alive = false;
                    }

                    for (uint32_t n = work.NodeBegin(cell); n < work.NodeEnd(cell); ++n)
                    {
                        const Cand& node = work.nodes[n];
                        if (node.area == NavArea::Blocked || node.region < 0)
                        {
                            continue;
                        }

                        // Continue a run whose end steps directly onto this node.
                        size_t found = runs.size();
                        for (size_t r = 0; r < runs.size(); ++r)
                        {
                            if (runs[r].region != node.region ||
                                runs[r].at + 1 != uint16_t(t))
                            {
                                continue;
                            }
                            if (work.Neighbour(runs[r].last, along) == int32_t(n))
                            {
                                found = r;
                                break;
                            }
                        }

                        if (found == runs.size())
                        {
                            Run run;
                            run.first = uint16_t(t);
                            run.at = uint16_t(t);
                            run.last = n;
                            run.region = node.region;
                            run.firstZ = node.z;
                            run.width = node.clearance;
                            run.alive = true;
                            runs.push_back(run);
                            runNodes.push_back({n});
                        }
                        else
                        {
                            runs[found].at = uint16_t(t);
                            runs[found].last = n;
                            runs[found].width =
                                std::min(runs[found].width, node.clearance);
                            runs[found].alive = true;
                            runNodes[found].push_back(n);
                        }
                    }

                    // Close every run this position did not extend, and emit it.
                    for (size_t r = runs.size(); r-- > 0;)
                    {
                        if (runs[r].alive && t + 1 < CELLS_PER_TILE)
                        {
                            continue;
                        }

                        const Run& run = runs[r];

                        Gateway g;
                        g.side = side;
                        g.first = run.first;
                        g.last = run.at;
                        g.region = uint16_t(run.region);
                        g.firstZ = run.firstZ;
                        g.lastZ = work.nodes[run.last].z;
                        g.width = run.width;

                        const uint32_t mid = runNodes[r][runNodes[r].size() / 2];
                        const int midCell = work.nodeCell[mid];
                        const int mx = midCell / work.side;
                        const int my = midCell % work.side;
                        g.x = CellCentre(GlobalCell(work.tileX, mx - work.margin));
                        g.y = CellCentre(GlobalCell(work.tileY, my - work.margin));
                        g.z = work.nodes[mid].z;

                        out.push_back(g);
                        members.push_back(runNodes[r]);

                        runs.erase(runs.begin() + long(r));
                        runNodes.erase(runNodes.begin() + long(r));
                    }
                }
            }
        }

        /**
         * @brief What it really costs to cross the tile between each pair of gateways.
         *
         * One shortest-path search per gateway, seeded from every cell the gateway
         * covers at once, over the tile's own nodes. This is the second pass the whole
         * design turns on: it is paid offline, it depends on nothing outside this tile
         * -- so it can never go stale against a neighbour that was re-baked -- and it
         * is what makes the coarse search exact rather than merely admissible.
         *
         * A straight line between two gateways of a horseshoe-shaped region is half the
         * true distance. A coarse search believing it does not produce a wrong path, but
         * it produces the wrong CHOICE of path, and the mover walks visibly the long way
         * round for reasons no log explains.
         */
        void GatewayCosts(const Work& work, const std::vector<Gateway>& gateways,
                          const std::vector<std::vector<uint32_t>>& members,
                          std::vector<float>& out)
        {
            const size_t n = gateways.size();
            out.assign(n * n, NavTile::UNREACHABLE);

            if (n == 0)
            {
                return;
            }

            // Which gateway each node belongs to, so a finished search can be read off
            // without scanning every gateway's members for every node.
            std::vector<int32_t> owner(work.nodes.size(), -1);
            for (size_t g = 0; g < n; ++g)
            {
                for (uint32_t node : members[g])
                {
                    owner[node] = int32_t(g);
                }
            }

            std::vector<float> dist;
            using Entry = std::pair<float, uint32_t>;

            for (size_t g = 0; g < n; ++g)
            {
                dist.assign(work.nodes.size(), INF);
                std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

                for (uint32_t node : members[g])
                {
                    dist[node] = 0.0f;
                    open.push({0.0f, node});
                }

                size_t remaining = n;

                while (!open.empty() && remaining > 0)
                {
                    const Entry top = open.top();
                    open.pop();

                    if (top.first > dist[top.second])
                    {
                        continue;
                    }

                    const int32_t reached = owner[top.second];
                    if (reached >= 0 &&
                        out[g * n + size_t(reached)] == NavTile::UNREACHABLE)
                    {
                        out[g * n + size_t(reached)] = top.first;
                        --remaining;
                    }

                    for (int dir = 0; dir < DIRS; ++dir)
                    {
                        const int32_t to = work.Neighbour(top.second, dir);
                        if (to < 0 || !OwnNode(work, to))
                        {
                            continue;
                        }

                        const float next = top.first + StepLength(dir);
                        if (next < dist[size_t(to)])
                        {
                            dist[size_t(to)] = next;
                            open.push({next, uint32_t(to)});
                        }
                    }
                }
            }

            // The links were made undirected, so the matrix should already be
            // symmetric; taking the smaller of each pair costs nothing and makes that a
            // guarantee rather than an expectation the search would silently rely on.
            for (size_t a = 0; a < n; ++a)
            {
                for (size_t b = a + 1; b < n; ++b)
                {
                    const float best = std::min(out[a * n + b], out[b * n + a]);
                    out[a * n + b] = best;
                    out[b * n + a] = best;
                }
            }
        }

        /// Move the finished working grid into the tile that gets written.
        bool Emit(const Work& work, uint32_t regionCount, NavTile& out)
        {
            float minZ = INF;
            float maxZ = -INF;
            bool any = false;

            for (size_t n = 0; n < work.nodes.size(); ++n)
            {
                if (work.nodes[n].area == NavArea::Blocked || !OwnNode(work, int32_t(n)))
                {
                    continue;
                }

                minZ = std::min(minZ, work.nodes[n].z);
                maxZ = std::max(maxZ, work.nodes[n].z);
                any = true;
            }

            if (!any)
            {
                return false;
            }

            // A yard of slack under the lowest surface, so a height that rounds down
            // still quantises to something positive rather than saturating at zero.
            const float baseZ = minZ - 1.0f;
            if (maxZ - baseZ >= Z_SPAN)
            {
                // No 2.4.3 tile spans four thousand yards. If one ever does, refusing it
                // is right: the alternative is every surface above the span silently
                // collapsing onto the ceiling of the quantised range.
                return false;
            }

            out.Reset(work.tileX, work.tileY, baseZ);

            std::vector<Region>& regions = out.MutableRegions();
            regions.assign(size_t(regionCount), Region());
            for (Region& r : regions)
            {
                r.minZ = INF;
                r.maxZ = -INF;
            }

            for (int lx = 0; lx < CELLS_PER_TILE; ++lx)
            {
                for (int ly = 0; ly < CELLS_PER_TILE; ++ly)
                {
                    const int cell = work.CellAt(lx + work.margin, ly + work.margin);
                    const int inTile = lx * CELLS_PER_TILE + ly;

                    bool first = true;
                    for (uint32_t n = work.NodeBegin(cell); n < work.NodeEnd(cell); ++n)
                    {
                        const Cand& node = work.nodes[n];
                        if (node.area == NavArea::Blocked || node.region < 0)
                        {
                            continue;
                        }

                        const uint16_t region = uint16_t(node.region);
                        const uint8_t clearance = QuantiseClearance(node.clearance);
                        const uint16_t z = QuantiseZ(node.z, baseZ);

                        Region& r = regions[region];
                        ++r.cellCount;
                        r.minZ = std::min(r.minZ, node.z);
                        r.maxZ = std::max(r.maxZ, node.z);
                        r.areas |= AreaBit(node.area);
                        r.maxClearance = std::max(r.maxClearance, node.clearance);

                        if (first)
                        {
                            out.SetCell(inTile, z, PackArea(node.area, node.flags),
                                        clearance, region);
                            first = false;
                        }
                        else
                        {
                            StackedLayer layer;
                            layer.cell = uint32_t(inTile);
                            layer.z = z;
                            layer.region = region;
                            layer.area = PackArea(node.area, node.flags);
                            layer.clearance = clearance;
                            out.AddStacked(layer);

                            // The dense cell has to advertise that there is more, or
                            // nothing will ever look in the stacked table for it.
                            out.MutableAreaPlane()[size_t(inTile)] |= CELL_STACKED;
                        }
                    }
                }
            }

            out.SortStacked();

            for (Region& r : regions)
            {
                if (r.cellCount == 0)
                {
                    r.minZ = 0.0f;
                    r.maxZ = 0.0f;
                }
            }

            return true;
        }
    }

    bool BuildNavTile(const world::terrain::FusedTerrain& terrain, int tileX, int tileY,
                      const BuildParams& params, NavTile& out)
    {
        Work work;
        work.margin = std::max(0, params.margin);
        work.side = CELLS_PER_TILE + 2 * work.margin;
        work.tileX = tileX;
        work.tileY = tileY;

        Sample(terrain, params, work);
        Connect(work, params);
        Slope(work, params);
        Clearance(work);

        const uint32_t regionCount = Regionise(work);
        if (regionCount == 0)
        {
            return false;
        }

        if (!Emit(work, regionCount, out))
        {
            return false;
        }

        // Carried into the tile so the runtime joins two tiles at their border with the
        // same climb limit the bake used everywhere else. See NavTile::TileParams.
        TileParams stored;
        stored.agentHeight = params.agentHeight;
        stored.maxClimb = params.maxClimb;
        stored.maxSlopeDeg = params.maxSlopeDeg;
        stored.swimDepth = params.swimDepth;
        out.SetParams(stored);

        std::vector<std::vector<uint32_t>> members;
        FindGateways(work, out.MutableGateways(), members);
        GatewayCosts(work, out.Gateways(), members, out.MutableGatewayCost());

        return true;
    }
}

