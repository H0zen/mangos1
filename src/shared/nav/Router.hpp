#pragma once

// Finding the way, over AREAS. One engine, two stages.
//
// == 1. Coarse: over rectangles ==
//
// The map, seen as convex areas and the openings between them. Inside a tile a step is a
// portal the mesh already records; at a tile's rim it is a crossing the store matched
// between two resident meshes. A search over this settles which way round the mountain
// in a handful of expansions, over a couple of thousand areas per tile rather than a
// quarter of a million cells.
//
// == 2. Fine: Polyanya, once per tile crossed ==
//
// Between the point the route enters a tile and the point it leaves, the shortest path
// over that tile's mesh, computed exactly in one pass. Its states are intervals of
// points on the openings, so a continuum of routes stays alive until the geometry
// separates them -- and the points it returns are already the turns. There is no
// smoothing stage, because there is nothing left to smooth.
//
// == 3. Links: the ground that does not join ==
//
// A hand-authored jump -- off the Booty Bay dock, between the ledges of Blade's Edge --
// is an edge of the COARSE search and of nothing else. The fine stage is never shown one,
// because there is nothing between the two mouths for it to be shown: the leg is cut at
// the near mouth, the jump is emitted as a single segment, and Polyanya is asked again
// from the far one. Nothing walks a link, which is the truth about it.
//
// == The engine that used to be here ==
//
// Gateways with a baked cost matrix, a fine search over cells, and an emitter that
// flattened a cell path back into corners. All three are gone. What remains of that
// design is marked MARKED FOR DELETION where it stands -- `GateRef`, `Crossing`,
// `NavStore::CrossingsOf` and `StitchLocked` in NavStore.hpp, the border `Gateway` and
// `GatewayCost` in NavTile.hpp, `FindGateways` and `GatewayCosts` in NavBuilder.cpp --
// and it survives only because the tile FILE still carries the section. It goes at the
// next format bump.
//
// Two engines answering one question is how they came to disagree about area, clearance,
// length and floor -- one said a bridge was walkable and the other routed under it.
//
// == What this deliberately does NOT do ==
//
// It never decides what a mover is ALLOWED to do; that arrives as a MoveProfile. It
// never falls back to a straight line either: a straight line has to be laid on the
// ground, the ground is the terrain engine's, and a router that reached for the terrain
// would be a router that could not be tested without one. Off-mesh is reported as a
// fact, and what to do about it belongs to the caller.

#include "nav/NavStore.hpp"
#include "nav/Polyanya.hpp"
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
            /**
             * @brief THE WHOLE ROUTE, over areas rather than cells.
             *
             * One search, two stages, and neither of them touches a cell:
             *
             *  - coarse: A* over (tile, rectangle) pairs, stepping through the openings
             *    a mesh already records and, at a tile's rim, through the crossings the
             *    store matched between two resident meshes. This is what the gateway
             *    graph and its baked cost matrix become when the areas ARE the structure.
             *  - fine: Polyanya between the entry and exit point of each tile the
             *    corridor passes through, which returns the shortest path over that
             *    tile's mesh in one pass, already taut.
             *
             * @return False when no route exists over the mesh. There is nothing else
             *         to ask: this is the engine, not one of two.
             */
            bool FindOnMesh(const RouteRequest& request, const CellRef& startCell,
                            const Surface& startSurface, const CellRef& endCell,
                            const Surface& endSurface, Route& out) const;

            /// One step of the corridor: which tile, which rectangle, and where the
            /// route entered it.
            struct MeshStep
            {
                int tileX = 0;
                int tileY = 0;
                uint32_t rect = 0;

                /// Where the route entered this area.
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;

                /**
                 * @brief This area was entered by a JUMP, not by walking into it.
                 *
                 * The corridor has to carry this, because it is the one thing the fine
                 * stage may not discover for itself: Polyanya asked to cross from the
                 * near mouth to the far one would report a wall, correctly -- there is no
                 * ground between them, which is what makes it a link. So the leg is cut
                 * at `fromX/Y/Z`, the jump is emitted as a single segment, and the next
                 * leg starts from `x/y/z`.
                 */
                bool byLink = false;

                /**
                 * @brief The route left the previous area somewhere ELSE than it arrived.
                 *
                 * True at a tile border and at a link, and false for an ordinary opening
                 * inside a tile. Both are handovers: one leg ends at `fromX/Y/Z`, the
                 * next begins at `x/y/z`, and the two are not the same point -- a border
                 * has a cell on each side of it, and a link has two mouths.
                 *
                 * That distinction is the whole of the inter-tile bug. A corridor that
                 * recorded one point per step could only hand the next tile's search a
                 * position inside the PREVIOUS tile, which that search then refused, so
                 * no route across a tile border was ever produced.
                 */
                bool handover = false;

                /// Where the leg before this one ended. Only meaningful when `handover`.
                float fromX = 0.0f;
                float fromY = 0.0f;
                float fromZ = 0.0f;
            };

            /// Stage one on the mesh. Empty when no way across exists.
            bool CoarseOnMesh(const CellRef& startCell, const CellRef& endCell,
                              const Geometry::Vector3& from,
                              const Geometry::Vector3& to,
                              const MoveProfile& profile,
                              std::vector<MeshStep>& corridor) const;

            /**
             * @brief Write out a path that arrived already taut.
             *
             * A mesh path arrives already taut: its points ARE the turns, each one a
             * corner the search bent around because the geometry made it bend. There is
             * nothing to simplify and it would be wrong to try -- dropping one of these
             * points does not shorten the route, it cuts a corner the mesh says is
             * solid. (The engine this replaced needed a whole stage for that, because a
             * cell path is a bead every 0.7 yards and none of them is a decision.)
             *
             * What remains is seating. The search works in plan, and the answer has to
             * come back with a height on it; each point is put on the surface under the
             * height the previous one ended at, so a route up a ramp climbs it instead
             * of interpolating through it.
             *
             * @param endZ the height of THIS leg's last point. A leg that ends at a tile
             *             crossing or at the mouth of a link ends there and not at the
             *             caller's destination -- passing the destination's height to
             *             every leg put each intermediate handover at the elevation of a
             *             place the mover has not reached yet.
             */
            /// @param endArea what the leg's last point stands on, so the body can be
            ///        seated there the same way every point before it was. A leg that
            ///        ends at a tile crossing hands on the ground it crossed; the last
            ///        leg of all hands on the destination's own surface.
            void EmitMeshPath(const NavTile& tile, const MeshPath& path,
                              const RouteRequest& request, float endZ,
                              NavArea endArea, Route& out) const;

            const NavStore& m_store;
    };
}
