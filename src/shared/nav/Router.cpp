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

        /**
         * @brief Layers of one cell a step will consider.
         *
         * Generous on purpose. The first cut at this was FOUR, on the reasoning that
         * nothing stacks more floors than that under one square of ground -- which is
         * simply false for the places that need routing most. Blackrock Depths,
         * Blackrock Spire, Karazhan and the wells of Ironforge all pile up well past
         * four, and the failure would have been quiet: the data still holds every
         * surface, so nothing looks missing, and only the upper floors are unroutable.
         */
        constexpr uint16_t MAX_SEARCH_LAYERS = 64;

        inline int LocalX(int inTile) { return inTile / CELLS_PER_TILE; }
        inline int LocalY(int inTile) { return inTile % CELLS_PER_TILE; }

        inline int InTileOf(int localX, int localY)
        {
            return localX * CELLS_PER_TILE + localY;
        }

        float Dist2D(const Geometry::Vector3& a, const Geometry::Vector3& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        /// May this mover stand on this surface at all? Area and width, and the area half
        /// of it is `MoveProfile`'s own -- see `AdmitsGround`, which is where the rule
        /// about a walker not surfacing halfway across a bay lives now that the mesh
        /// search and this one both have to obey it.
        bool Admits(const MoveProfile& profile, const Surface& surface)
        {
            return surface.Valid() && profile.AdmitsGround(surface.area) &&
                   profile.Fits(surface.clearance);
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

        /// Is a local cell coordinate inside the tile?
        inline bool InTileBounds(int localX, int localY)
        {
            return localX >= 0 && localX < CELLS_PER_TILE && localY >= 0 &&
                   localY < CELLS_PER_TILE;
        }

        /**
         * @brief One step onto a neighbouring cell.
         *
         * Diagonals refuse to cut corners: both orthogonal steps the diagonal is made of
         * must themselves be walkable. Without it a line slips diagonally between two
         * buildings through a gap of exactly zero yards -- which the client will not
         * follow, so the creature stops dead against the corner.
         */
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
         * @brief Cut a finished route at a length, in yards. True when it cut.
         *
         * The cut lands ON the segment rather than before it. A taut path's segment is a
         * straight run the client will walk in one go, so a point partway along it is a
         * place the mover really passes through -- and stopping at the previous CORNER
         * instead would leave a thirty-yard flee ending wherever the last bend happened
         * to be, which on open ground is the start.
         *
         * What it must never do is move the final point to the caller's destination
         * afterwards. A route that has been cut is precisely a route that does not reach
         * the goal, and welding the goal back onto its end replaces walked ground with a
         * straight line through everything the search never looked at.
         */
        bool ClipToLength(float maxLength, std::vector<Geometry::Vector3>& points)
        {
            if (maxLength <= 0.0f || points.size() < 2)
            {
                return false;
            }

            float walked = 0.0f;

            for (size_t i = 1; i < points.size(); ++i)
            {
                const float leg = Dist2D(points[i - 1], points[i]);
                if (walked + leg <= maxLength)
                {
                    walked += leg;
                    continue;
                }

                const float t = leg > 0.0f ? (maxLength - walked) / leg : 0.0f;

                // A cut a hair past a corner is a zero-length segment, and the wire packs
                // one of those into a division by zero on the client. Drop the point
                // instead; the caller sees a route one corner shorter, which is true.
                if (t * leg < 0.01f)
                {
                    points.resize(i);
                    return true;
                }

                points[i] = points[i - 1] + (points[i] - points[i - 1]) * t;
                points.resize(i + 1);
                return true;
            }

            return false;
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

        // The stricter of the bake's limit and the mover's, never a larger one -- the
        // same rule the bake linked its own cells with, for the same reason.
        const float climb = ClimbWindow(
            std::min(tile->Params().maxClimb, profile.maxClimb),
            tile->Params().maxSlopeDeg, CELL_SIZE);

        // === The cells the SEGMENT crosses, not a walk that merely ends where it ends.
        //
        // This used to step greedily towards the target, diagonally while both axes had
        // ground left, and it answered a different question than the one being asked.
        // The caller is deciding whether to drop a corner from the route -- which means
        // the CLIENT will walk the straight chord between the two surviving points. A
        // greedy walk can go round the corner of a building and report success, and the
        // chord then clips that corner: the creature walks into the wall, stops, and the
        // route it was given never mentioned the obstacle.
        //
        // So walk the segment's supercover: every cell it actually enters, in order,
        // by the standard grid traversal. One axis crosses at a time, so each step is
        // orthogonal -- except where the chord passes exactly through a cell corner,
        // which is a diagonal and gets the corner rule.
        //
        // The surface is carried forward through all of it. That is what tells a line
        // crossing four half-yard steps (fine) from one crossing a single ten-yard drop
        // (not fine), and no per-cell test can.
        const float originX = float(tile->TileX() * CELLS_PER_TILE);
        const float originY = float(tile->TileY() * CELLS_PER_TILE);

        const float px = CellCoord(from.x) - originX;
        const float py = CellCoord(from.y) - originY;
        const float qx = CellCoord(to.x) - originX;
        const float qy = CellCoord(to.y) - originY;

        const float dx = qx - px;
        const float dy = qy - py;

        int ix = LocalX(fromCell.InTile());
        int iy = LocalY(fromCell.InTile());

        const int targetX = LocalX(toCell.InTile());
        const int targetY = LocalY(toCell.InTile());

        const int stepX = (dx > 0.0f) ? 1 : ((dx < 0.0f) ? -1 : 0);
        const int stepY = (dy > 0.0f) ? 1 : ((dy < 0.0f) ? -1 : 0);

        // How far along the segment the next boundary crossing lies, per axis, and how
        // far apart consecutive crossings are. Both in units of the segment's length,
        // so the two axes are directly comparable and the smaller one is next.
        const float invX = (stepX != 0) ? 1.0f / std::fabs(dx) : 0.0f;
        const float invY = (stepY != 0) ? 1.0f / std::fabs(dy) : 0.0f;

        float tMaxX = (stepX > 0) ? (float(ix + 1) - px) * invX
                                  : ((stepX < 0) ? (px - float(ix)) * invX : INF);
        float tMaxY = (stepY > 0) ? (float(iy + 1) - py) * invY
                                  : ((stepY < 0) ? (py - float(iy)) * invY : INF);

        const float tDeltaX = (stepX != 0) ? invX : INF;
        const float tDeltaY = (stepY != 0) ? invY : INF;

        Surface surface = fromSurface;

        // A segment inside one tile crosses at most one boundary per cell per axis.
        int guard = CELLS_PER_TILE * 3;

        while ((ix != targetX || iy != targetY) && guard-- > 0)
        {
            // Equal within a rounding of each other means the chord goes through the
            // corner exactly. Taking two orthogonal steps there would visit a cell the
            // chord never enters -- and refuse the line because of it -- so it is one
            // diagonal step, which Neighbour already guards with the corner rule.
            const bool corner = std::fabs(tMaxX - tMaxY) < 1e-6f;

            int dir = -1;
            if (corner && stepX != 0 && stepY != 0)
            {
                dir = (stepX < 0) ? ((stepY < 0) ? 4 : 5) : ((stepY < 0) ? 6 : 7);
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
                ix += stepX;
                iy += stepY;
            }
            else if (tMaxX < tMaxY)
            {
                dir = (stepX < 0) ? 0 : 1;
                tMaxX += tDeltaX;
                ix += stepX;
            }
            else
            {
                dir = (stepY < 0) ? 2 : 3;
                tMaxY += tDeltaY;
                iy += stepY;
            }

            if (!InTileBounds(ix, iy))
            {
                return false;
            }

            int nextCell = 0;
            Surface next;
            if (!Neighbour(*tile, InTileOf(ix - DX[dir], iy - DY[dir]), surface, dir,
                           profile, climb, nextCell, next))
            {
                return false;
            }

            surface = next;
        }

        return ix == targetX && iy == targetY;
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

        // THE MESH IS THE ROUTE. Areas and the openings between them, a coarse search
        // over those, and Polyanya inside each tile -- no cells anywhere in it, and the
        // points come back already taut so there is nothing to straighten afterwards.
        //
        // What follows it is not a second implementation kept for taste. A tile can be
        // resident before its mesh has been derived, and a query arriving in that window
        // still has to be answered; deriving one here would put a pass over a quarter of
        // a million cells on the map's tick.
        if (FindOnMesh(request, startCell, startSurface, endCell, endSurface, out))
        {
            // THE LENGTH CAP, measured in yards over the points actually emitted. It is
            // the only place it can be measured: a point stands for a corner, so the
            // count of them is not a distance and never was. Flee and confused movement
            // are written round a thirty-yard ceiling, and between the emitter changing
            // and this line the mesh route ignored it -- a ten-yard bolt could come back
            // two hundred yards long and be reported as a complete success.
            //
            // Before the point budget, because cutting the route may bring it under.
            if (ClipToLength(request.budget.maxLength, out.points))
            {
                if (out.points.size() < 2)
                {
                    // Not even one segment fits inside the cap. There is no route of this
                    // length, which is a different thing from a wall.
                    out.Clear();
                    out.outcome = RouteOutcome::Unroutable;
                    out.stop = RouteStop::LengthBudget;
                    return;
                }

                out.outcome = RouteOutcome::Partial;
                out.stop = RouteStop::LengthBudget;
                return;
            }

            if (out.points.size() <= request.budget.points)
            {
                out.outcome = RouteOutcome::Routed;
                out.stop = RouteStop::Reached;
            }
            else
            {
                out.outcome = RouteOutcome::Partial;
                out.stop = RouteStop::PointBudget;
            }
            return;
        }

        // Nothing else to try. The cell engine that used to sit here -- Coarse over
        // gateways, Refine over cells, Emit to flatten a cell path back into corners --
        // is gone; what is left of it is MARKED FOR DELETION where it stands, and the
        // header lists it. It was not a fallback, it was a
        // second answer: it admitted areas the mesh refused, priced them differently,
        // ignored the floor a point was on, and enforced a length limit the mesh did
        // not. Two engines for one question is how a bridge became walkable to one of
        // them and a thing to route under for the other.
        out.outcome = RouteOutcome::Unroutable;
        out.stop = RouteStop::Wall;
    }

    // ------------------------------------------------------------------ coarse ----

    bool Router::CoarseOnMesh(const CellRef& startCell, const CellRef& endCell,
                              const Geometry::Vector3& from,
                              const Geometry::Vector3& to,
                              const MoveProfile& profile,
                              std::vector<MeshStep>& corridor) const
    {
        corridor.clear();

        const auto pack = [](int tileX, int tileY, uint32_t rect)
        {
            return (static_cast<uint64_t>(static_cast<uint16_t>(tileX)) << 48) |
                   (static_cast<uint64_t>(static_cast<uint16_t>(tileY)) << 32) |
                   static_cast<uint64_t>(rect);
        };

        struct Entry
        {
            float g = 0.0f;
            uint64_t parent = 0;
            bool hasParent = false;

            /// Where the route entered this area.
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;

            /// It got in by a jump, from this mouth on the area before it.
            bool byLink = false;

            /// The route left the previous area at a DIFFERENT point from the one it
            /// arrived at: a tile border, or the two mouths of a link. True for both,
            /// because both end a leg at one place and start the next at another.
            bool handover = false;
            float fromX = 0.0f;
            float fromY = 0.0f;
            float fromZ = 0.0f;
        };

        const std::shared_ptr<const TileMesh> startMesh =
            m_store.MeshOf(startCell.TileX(), startCell.TileY());
        const std::shared_ptr<const TileMesh> endMesh =
            m_store.MeshOf(endCell.TileX(), endCell.TileY());
        const std::shared_ptr<const NavTile> startTile = m_store.TileOf(startCell);
        const std::shared_ptr<const NavTile> endTile = m_store.TileOf(endCell);

        if (!startMesh || !endMesh || !startTile || !endTile)
        {
            return false;
        }

        const int32_t startRect =
            RectAtHeight(*startTile, *startMesh, from.x, from.y, from.z);
        const int32_t endRect = RectAtHeight(*endTile, *endMesh, to.x, to.y, to.z);
        if (startRect < 0 || endRect < 0)
        {
            return false;
        }

        const uint8_t needed = QuantiseClearance(profile.radius);

        const uint64_t goal =
            pack(endCell.TileX(), endCell.TileY(), static_cast<uint32_t>(endRect));

        std::unordered_map<uint64_t, Entry> seen;
        using Open = std::pair<float, uint64_t>;
        std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;

        const auto heuristic = [&to](float x, float y)
        {
            const float dx = to.x - x;
            const float dy = to.y - y;
            return std::sqrt(dx * dx + dy * dy);
        };

        {
            const uint64_t key = pack(startCell.TileX(), startCell.TileY(),
                                      static_cast<uint32_t>(startRect));
            Entry& entry = seen[key];
            entry.g = 0.0f;
            entry.x = from.x;
            entry.y = from.y;
            entry.z = from.z;
            open.push({heuristic(from.x, from.y), key});
        }

        // A budget in nodes, not in cells: a tile holds a couple of thousand areas
        // against a quarter of a million cells, so a corridor across a continent is
        // hundreds of expansions rather than hundreds of thousands.
        uint32_t expansions = 0;
        constexpr uint32_t MAX_EXPANSIONS = 20000;

        bool reached = false;

        while (!open.empty() && expansions < MAX_EXPANSIONS)
        {
            const Open top = open.top();
            open.pop();
            ++expansions;

            const uint64_t key = top.second;
            const Entry here = seen[key];

            if (top.first > here.g + heuristic(here.x, here.y) + 0.001f)
            {
                continue;   // superseded by a cheaper way to the same area
            }

            if (key == goal)
            {
                reached = true;
                break;
            }

            const int tileX = static_cast<int16_t>(key >> 48);
            const int tileY = static_cast<int16_t>(key >> 32);
            const uint32_t rect = static_cast<uint32_t>(key);

            const std::shared_ptr<const NavTile> tile = m_store.TileAt(tileX, tileY);
            const std::shared_ptr<const TileMesh> mesh = m_store.MeshOf(tileX, tileY);
            if (!tile || !mesh || rect >= mesh->rects.size())
            {
                continue;
            }

            const auto relax = [&](uint64_t next, float x, float y, float z, float step)
            {
                const float g = here.g + step;
                const auto it = seen.find(next);
                if (it != seen.end() && it->second.g <= g)
                {
                    return;
                }

                Entry& entry = seen[next];
                entry.g = g;
                entry.parent = key;
                entry.hasParent = true;
                entry.x = x;
                entry.y = y;
                entry.z = z;
                entry.byLink = false;
                entry.handover = false;
                open.push({g + heuristic(x, y), next});
            };

            // May this mover enter an area at all? Width and permission together, and
            // the permission half is `MoveProfile`'s own so that this and Polyanya
            // cannot come to different conclusions about the same rectangle -- which is
            // exactly what happened while one of them read only the width.
            const auto admits = [&](const NavRect& area)
            {
                return area.clearance >= needed && profile.AdmitsGround(area.area);
            };

            // Inside the tile: every opening this area has.
            for (uint32_t i = mesh->first[rect]; i < mesh->first[rect + 1]; ++i)
            {
                const Portal& portal = mesh->portals[i];
                if (portal.LeavesTheTile() || portal.clearance < needed ||
                    !admits(mesh->rects[portal.neighbour]))
                {
                    continue;
                }

                float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
                PortalSegment(*tile, *mesh, portal, ax, ay, bx, by);

                const float mx = (ax + bx) * 0.5f;
                const float my = (ay + by) * 0.5f;
                const float dx = mx - here.x;
                const float dy = my - here.y;

                // === WHERE AREA COST LIVES.
                //
                // Here, and deliberately not inside Polyanya. This stage is a search
                // over discrete areas, so a multiplier on the step into one is exactly
                // what `areaCost` means -- wading is dearer than the dry ground beside
                // it, and steep ground is dearer than level. The fine stage cannot take
                // it: a cost that varies across the plan makes the optimal path REFRACT
                // at the boundary rather than run straight, and the straight line inside
                // a convex area is what its whole method rests on.
                //
                // The heuristic stays admissible as long as no multiplier is below one,
                // which is what `areaCost` documents itself to mean.
                relax(pack(tileX, tileY, portal.neighbour), mx, my,
                      (portal.loZ + portal.hiZ) * 0.5f,
                      std::sqrt(dx * dx + dy * dy) *
                          profile.PenaltyOf(mesh->rects[portal.neighbour].area));
            }

            // Out of the tile: what the store matched between two resident rims.
            for (const MeshCrossing& crossing :
                 m_store.MeshCrossingsOf(tileX, tileY, rect))
            {
                if (crossing.clearance < needed)
                {
                    continue;
                }

                const std::shared_ptr<const TileMesh> farMesh =
                    m_store.MeshOf(crossing.farTileX, crossing.farTileY);
                if (!farMesh || crossing.farRect >= farMesh->rects.size())
                {
                    continue;
                }

                const NavRect& far = farMesh->rects[crossing.farRect];
                if (!admits(far))
                {
                    continue;
                }

                const float dx = crossing.x - here.x;
                const float dy = crossing.y - here.y;

                // The area is entered at the FAR point, and the near point is where the
                // route leaves this tile. Recording only one of them is what made every
                // inter-tile route fail: the next tile's search was started at a
                // position that lies in the previous tile, and refused it.
                const uint64_t next =
                    pack(crossing.farTileX, crossing.farTileY, crossing.farRect);
                const float g =
                    here.g + std::sqrt(dx * dx + dy * dy) * profile.PenaltyOf(far.area);

                const auto seat = seen.find(next);
                if (seat != seen.end() && seat->second.g <= g)
                {
                    continue;
                }

                Entry& entry = seen[next];
                entry.g = g;
                entry.parent = key;
                entry.hasParent = true;
                entry.x = crossing.farX;
                entry.y = crossing.farY;
                entry.z = crossing.farZ;
                entry.byLink = false;
                entry.fromX = crossing.x;
                entry.fromY = crossing.y;
                entry.fromZ = crossing.z;
                entry.handover = true;
                open.push({g + heuristic(crossing.farX, crossing.farY), next});
            }

            // === Off the ground entirely: the hand-authored crossings.
            //
            // A link is an edge of this graph and of no other. The fine stage is never
            // shown one, because there is nothing between the two mouths for it to be
            // shown -- so what the corridor records is that the far area was entered by
            // a jump and from which point, and the leg is cut there.
            //
            // Scanned rather than indexed: 2.4.3 authors three of these in the whole
            // game, and an index over a list that short costs more to keep correct than
            // it can save.
            for (const MeshLink& link : mesh->links)
            {
                const bool forward = link.fromRect == rect;
                const bool backward = link.bidirectional != 0 && link.toRect == rect;
                if (!forward && !backward)
                {
                    continue;
                }

                const uint32_t target = forward ? link.toRect : link.fromRect;
                if (link.clearance < needed || target >= mesh->rects.size() ||
                    !admits(mesh->rects[target]))
                {
                    continue;
                }

                const float mouthX = forward ? link.fromX : link.toX;
                const float mouthY = forward ? link.fromY : link.toY;
                const float mouthZ = forward ? link.fromZ : link.toZ;
                const float landX = forward ? link.toX : link.fromX;
                const float landY = forward ? link.toY : link.fromY;
                const float landZ = forward ? link.toZ : link.fromZ;

                // Walking to the mouth, and then the jump at its authored price. The
                // walk is priced by the area it crosses; the jump is not, because
                // nothing is crossed.
                const float dx = mouthX - here.x;
                const float dy = mouthY - here.y;
                const float walk = std::sqrt(dx * dx + dy * dy) *
                                   profile.PenaltyOf(mesh->rects[rect].area);
                const float step = walk + link.cost;

                const uint64_t next = pack(tileX, tileY, target);
                const float g = here.g + step;

                const auto it = seen.find(next);
                if (it != seen.end() && it->second.g <= g)
                {
                    continue;
                }

                Entry& entry = seen[next];
                entry.g = g;
                entry.parent = key;
                entry.hasParent = true;
                entry.x = landX;
                entry.y = landY;
                entry.z = landZ;
                entry.byLink = true;
                entry.handover = true;
                entry.fromX = mouthX;
                entry.fromY = mouthY;
                entry.fromZ = mouthZ;
                open.push({g + heuristic(landX, landY), next});
            }
        }

        if (!reached)
        {
            return false;
        }

        for (uint64_t at = goal;; at = seen[at].parent)
        {
            MeshStep step;
            step.tileX = static_cast<int16_t>(at >> 48);
            step.tileY = static_cast<int16_t>(at >> 32);
            step.rect = static_cast<uint32_t>(at);
            step.x = seen[at].x;
            step.y = seen[at].y;
            step.z = seen[at].z;
            step.byLink = seen[at].byLink;
            step.handover = seen[at].handover;
            step.fromX = seen[at].fromX;
            step.fromY = seen[at].fromY;
            step.fromZ = seen[at].fromZ;
            corridor.push_back(step);

            if (!seen[at].hasParent)
            {
                break;
            }
        }

        std::reverse(corridor.begin(), corridor.end());
        return true;
    }

    bool Router::FindOnMesh(const RouteRequest& request, const CellRef& startCell,
                            const Surface& startSurface, const CellRef& endCell,
                            const Surface& endSurface, Route& out) const
    {
        (void)startSurface;

        const std::shared_ptr<const NavTile> startTile = m_store.TileOf(startCell);
        const std::shared_ptr<const TileMesh> startMesh =
            m_store.MeshOf(startCell.TileX(), startCell.TileY());
        if (!startTile || !startMesh)
        {
            return false;
        }

        // One tile, and the ground joins: no corridor to plan, the mesh of that tile is
        // the whole problem and Polyanya answers it exactly in one pass.
        //
        // Falling THROUGH this rather than returning is what a link needs. Two mouths
        // either side of a wall are two areas of one tile with no opening between them,
        // so the fine search says no -- correctly, there is no ground -- and the coarse
        // stage below is the only thing that can find the jump. It runs on failure only,
        // so the ordinary local route still costs one search.
        if (startCell.TileX() == endCell.TileX() &&
            startCell.TileY() == endCell.TileY())
        {
            MeshQuery query;
            query.startX = request.start.x;
            query.startY = request.start.y;
            query.startZ = request.start.z;
            query.endX = request.end.x;
            query.endY = request.end.y;
            query.endZ = request.end.z;
            query.profile = request.profile;

            const MeshPath path = FindMeshPath(*startTile, *startMesh, query);
            if (path.found)
            {
                EmitMeshPath(*startTile, path, request, endSurface.z, out);
                return true;
            }

            if (startMesh->links.empty())
            {
                return false;
            }
        }

        std::vector<MeshStep> corridor;
        if (!CoarseOnMesh(startCell, endCell, request.start, request.end,
                          request.profile, corridor))
        {
            return false;
        }

        // Walk the corridor leg by leg. Consecutive steps in the SAME tile are one
        // stretch of walking, and Polyanya answers it exactly -- so the fine stage runs
        // once per tile crossed rather than once per area, and the points it returns are
        // already the turns.
        //
        // A leg also ends at the mouth of a link, and that is the second reason the run
        // is not simply "steps of one tile": the mover walks to the lip of the dock, and
        // what happens next is not walking.
        out.points.clear();

        Geometry::Vector3 at = request.start;

        for (size_t i = 0; i < corridor.size();)
        {
            // A leg runs until the next step is a HANDOVER -- a tile border or a link.
            // Both end the walk at one point and resume it at another, and neither can
            // be crossed by the fine search: one is another file's ground, the other is
            // no ground at all.
            size_t j = i;
            while (j + 1 < corridor.size() && !corridor[j + 1].handover)
            {
                ++j;
            }

            const bool last = j + 1 >= corridor.size();

            const std::shared_ptr<const NavTile> tile =
                m_store.TileAt(corridor[i].tileX, corridor[i].tileY);
            const std::shared_ptr<const TileMesh> mesh =
                m_store.MeshOf(corridor[i].tileX, corridor[i].tileY);
            if (!tile || !mesh)
            {
                return false;
            }

            // Where this leg stops: the caller's destination, or the point the corridor
            // recorded as the place it left this tile -- which is on THIS side of the
            // border, and is a different point from the one the next leg starts at.
            const Geometry::Vector3 leave =
                last ? request.end
                     : Geometry::Vector3(corridor[j + 1].fromX, corridor[j + 1].fromY,
                                         corridor[j + 1].fromZ);

            MeshQuery query;
            query.startX = at.x;
            query.startY = at.y;
            query.startZ = at.z;
            query.endX = leave.x;
            query.endY = leave.y;
            query.endZ = leave.z;
            query.profile = request.profile;

            const MeshPath path = FindMeshPath(*tile, *mesh, query);
            if (!path.found)
            {
                return false;
            }

            RouteRequest leg = request;
            leg.start = at;
            leg.end = leave;

            Route piece;

            // The leg's OWN end height. Handing every leg the destination's would put
            // each handover at the elevation of somewhere the mover has not reached.
            EmitMeshPath(*tile, path, leg, last ? endSurface.z : leave.z, piece);

            // The first point of a leg is the last point of the one before it. Dropped
            // rather than emitted twice: a repeated point is a zero-length segment, and
            // the wire's packing turns one of those into a division by zero on the
            // client.
            for (size_t k = out.points.empty() ? 0 : 1; k < piece.points.size(); ++k)
            {
                out.points.push_back(piece.points[k]);
            }

            at = leave;

            if (!last)
            {
                // The handover itself, as one segment. Across a border that is a step of
                // about a yard from one tile's border cell to the other's; across a link
                // it is the jump. Neither is walked ground, so neither is seated against
                // any -- the far end is where the geometry, or the author, says it lands.
                at = Geometry::Vector3(corridor[j + 1].x, corridor[j + 1].y,
                                       corridor[j + 1].z);
                out.points.push_back(at);
            }

            i = j + 1;
        }

        return out.points.size() >= 2;
    }

    void Router::EmitMeshPath(const NavTile& tile, const MeshPath& path,
                              const RouteRequest& request, float endZ,
                              Route& out) const
    {
        out.points.clear();
        if (path.points.empty())
        {
            return;
        }

        out.points.reserve(path.points.size());

        // The search works between cell centres; the caller asked about two exact
        // positions. Substituting them at the ends is not a fudge -- both lie in the
        // rectangles the first and last points came from, and the segments to them stay
        // inside those rectangles because a rectangle is convex.
        float height = request.start.z;

        for (size_t i = 0; i < path.points.size(); ++i)
        {
            const bool first = i == 0;
            const bool last = i + 1 == path.points.size();

            float x = path.points[i].x;
            float y = path.points[i].y;
            if (first)
            {
                x = request.start.x;
                y = request.start.y;
            }
            else if (last)
            {
                x = request.end.x;
                y = request.end.y;
            }

            float z = last ? endZ : height;
            NavArea area = NavArea::Ground;

            if (!first && !last)
            {
                const CellRef cell = CellAt(x, y);
                if (cell.Valid() && cell.TileX() == tile.TileX() &&
                    cell.TileY() == tile.TileY())
                {
                    // Seated against the height the walk arrived at, not against the
                    // start's: on a ramp those diverge by the whole climb, and a
                    // tolerance wide enough to cover it would let the seat jump to a
                    // floor above or below.
                    const Surface seated = tile.SurfaceUnder(
                        cell.InTile(), height, request.seatTolerance + CELL_SIZE);
                    if (seated.Valid())
                    {
                        z = seated.z;
                        area = AreaOf(seated.area);
                    }
                }
            }

            height = z;

            // === WHERE THE BODY SITS, not where the ground is.
            //
            // A point is a place to put a MOVER, and a mover is not a plane at the
            // surface: it stands a little above the floor, and a swimmer floats with its
            // head out rather than at the water's skin. Both figures are the terrain
            // engine's own, so a route that ignored them disagreed with every other
            // answer in the server about where a body at that position is.
            //
            // This lived in the emitter the cell engine had, and went out with it -- so
            // between that deletion and this line every routed point was seated exactly
            // on the geometry, which is the one height a body is never at.
            //
            // Not applied to the ends: those are the caller's own positions, and a
            // creature already standing somewhere does not need to be lifted off it.
            if (!first && !last)
            {
                z += area == NavArea::Water ? -SWIM_SEAT_DEPTH : GROUND_CLEARANCE;
            }

            out.points.push_back(Geometry::Vector3(x, y, z));
        }
    }

}
