#pragma once

// Finding the way, in three stages, each of which does only what the one below it
// cannot.
//
// == 1. Coarse: over gateways ==
//
// The whole map, seen as a few dozen doorways per tile. Edges inside a tile cost what
// the baker MEASURED between those two gateways -- not a straight line, a real walked
// distance -- and edges between tiles are the joins the store computed when both tiles
// arrived. A search over this settles which way round the mountain in a handful of
// expansions, and it is exact, so the answer does not have to be second-guessed later.
//
// == 2. Fine: over cells, one tile at a time ==
//
// The coarse stage hands down a sequence of gateways. Each consecutive pair lies in one
// tile, so each leg is a bounded search over that tile's cells and nothing else. This is
// where the mover's own size and permissions bite: the coarse stage rejects a gateway
// too narrow for him, and the fine stage rejects each cell that is.
//
// == 3. Emitting: over the cells that matter ==
//
// A cell path is not a route. Points are dropped wherever the way is straight -- proved
// by walking the line and checking it against the same cells -- so what leaves here is
// corners, not a bead for every 0.7 yards.
//
// == What this deliberately does NOT do ==
//
// It never decides what a mover is ALLOWED to do; that arrives as a MoveProfile. It
// never falls back to a straight line either: a straight line has to be laid on the
// ground, the ground is the terrain engine's, and a router that reached for the terrain
// would be a router that could not be tested without one. Off-mesh is reported as a
// fact, and what to do about it belongs to the caller.

#include "nav/NavStore.hpp"
#include "nav/Route.hpp"
#include "nav/SearchBudget.hpp"

#include "Geometry/Vector3.h"

namespace Nav
{
    /// One routing question, whole. Nothing is read from anywhere else.
    struct RouteRequest
    {
        Geometry::Vector3 start;
        Geometry::Vector3 end;
        MoveProfile profile;
        SearchBudget budget;

        /**
         * @brief Take the caller to the destination given, whatever the ground says.
         *
         * The route still follows real geometry as far as it can; this only decides
         * what happens at the end, where the goal does not sit on walkable ground.
         */
        bool forceDestination = false;

        /// How far above or below the given positions a surface may be and still count
        /// as the one they are on, in yards.
        float seatTolerance = 3.0f;
    };

    /**
     * @brief The router: a function over a store and a request.
     *
     * Holds no state between calls except the scratch buffers a search reuses, so two
     * consecutive requests cannot influence one another. That was worth being explicit
     * about: the router this replaces kept the previous corridor as a member and reused
     * it as a hint, which is a real optimisation and also the reason a stale budget from
     * one leg could silently cap the next.
     */
    class Router
    {
        public:
            explicit Router(const NavStore& store) : m_store(store) {}

            /// Answer one request. `out` is cleared first and always assigned.
            void Find(const RouteRequest& request, Route& out) const;

            /**
             * @brief Can a mover walk the straight segment between two points?
             *
             * Exposed because it answers a question the game asks constantly and
             * separately from routing -- may this creature charge from here to there --
             * and because the point emitter uses it to decide which corners matter.
             *
             * Only within one tile. Across a tile border it answers false, which is
             * conservative: the caller routes instead.
             */
            bool CanWalkLine(const Geometry::Vector3& from, const Geometry::Vector3& to,
                             const MoveProfile& profile, float tolerance = 3.0f) const;

        private:
            /// One stretch of the journey that lies inside a single tile, as the cells
            /// it crosses. The unit the fine stage produces and the emitter consumes.
            struct Leg
            {
                /// Held by shared_ptr, not by raw pointer. The store is shared between
                /// every instance of a map and each instance updates on its own thread,
                /// so a grid can unload while this route is still being built. The
                /// pointer keeps the tile alive for exactly as long as the leg refers
                /// to it.
                std::shared_ptr<const NavTile> tile;
                std::vector<std::pair<int, Surface>> cells;

                /// This leg is the far side of a hand-authored link: one cell, arrived
                /// at by crossing what the ground does not bridge. The emitter may not
                /// fold it into the step before it -- the take-off point is the whole
                /// content of the jump, and a route that lost it would walk a creature
                /// to a ledge and describe no ledge.
                bool jump = false;
            };

            /// Stage one: which gateways, in which order. Exact, because the cost of
            /// crossing a tile between two of its gateways was measured at bake time.
            bool Coarse(const CellRef& startCell, const Surface& startSurface,
                        const CellRef& endCell, const Surface& endSurface,
                        const MoveProfile& profile,
                        std::vector<GateRef>& corridor) const;

            /// Stage two: the cells, one tile at a time, between the gateways stage one
            /// chose. Returns false when a leg could not be walked -- the coarse stage
            /// plans for the most permissive mover, so this is where a corridor that is
            /// merely plausible for THIS mover is found out.
            bool Refine(const std::vector<GateRef>& corridor, const CellRef& startCell,
                        const Surface& startSurface, const CellRef& endCell,
                        const Surface& endSurface, const MoveProfile& profile,
                        uint32_t& budget, std::vector<Leg>& legs,
                        RouteStop& stop) const;

            /// Stage three: corners, not cells.
            void Emit(const std::vector<Leg>& legs, const RouteRequest& request,
                      Route& out) const;

            const NavStore& m_store;
    };
}
