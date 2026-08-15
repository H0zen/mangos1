#pragma once

// The answer to a routing request, and NOTHING that produces one. This header pulls in
// a vector of points and no more: no tiles, no store, no Unit, no Map. That is what lets
// the states below be asserted on in a test that links none of the server -- and the
// reason the states were worth extracting in the first place is that, as a bitmask
// inside the router, they could only ever be read by something that had a world.

#include "Geometry/Vector3.h"

#include <cstdint>
#include <vector>

namespace Nav
{
    /**
     * @brief What a routing attempt produced.
     *
     * An enumeration and not a bitmask, deliberately. The mask this replaces let states
     * be combined that nobody designed -- a result was routinely both "normal" and "not
     * using a path" -- and every consumer had to know which half of the field answered
     * its own question. Four states, mutually exclusive, and every consumer reads one
     * field.
     */
    enum class RouteOutcome : uint8_t
    {
        Routed,      ///< Real geometry, all the way to the goal.
        Partial,     ///< Real geometry, stopped short of the goal.
        Direct,      ///< No routing was used and that is ACCEPTABLE -- a swimmer or a
                     ///< flier off the mesh, or a map with no navigation at all. The
                     ///< points are a straight line laid onto the ground.
        Unroutable   ///< No route, and no fallback the mover is entitled to.
    };

    /**
     * @brief Why the route ended where it did.
     *
     * Distinct from the outcome because "stopped short" has causes that call for
     * opposite responses: a wall means the goal is unreachable from here, while a budget
     * means the search gave up and re-planning from further along makes progress.
     */
    enum class RouteStop : uint8_t
    {
        Reached,      ///< Arrived at the goal.
        Wall,         ///< The world stopped it short.
        NodeBudget,   ///< The fine search exhausted the cells it was allowed to expand.
        PointBudget,  ///< The route filled the points it was allowed to emit.

        /**
         * @brief The route ran past the yards the CALLER allowed it, and was cut there.
         *
         * Distinct from every other stop because nothing was wrong with the geometry: a
         * fleeing creature is held to thirty yards by a rule of the game, and reporting
         * that as `Wall` would tell the caller the world stopped it -- which invites a
         * re-plan that will be cut at exactly the same place, for ever.
         */
        LengthBudget,
        NoMesh,       ///< No navigation here, or the mover is exempt from using it.
        OffMesh,      ///< The start or the goal does not sit on walkable ground.
        TooNarrow,    ///< Ground exists all the way, and the mover is too wide for it.
        Forced,       ///< The caller demanded this destination whatever the geometry says.
        Failed        ///< The query itself errored.
    };

    /**
     * @brief The answer to one routing request: a value, owned by whoever asked.
     */
    struct Route
    {
        std::vector<Geometry::Vector3> points;
        RouteOutcome outcome = RouteOutcome::Unroutable;
        RouteStop    stop = RouteStop::Failed;

        void Clear()
        {
            points.clear();
            outcome = RouteOutcome::Unroutable;
            stop = RouteStop::Failed;
        }

        /// Reached the goal on real geometry. The strictest of the three.
        bool IsRouted() const { return outcome == RouteOutcome::Routed; }

        /**
         * @brief The points came off the baked navigation rather than out of a line.
         *
         * True of a PARTIAL route as well: stopping short does not make the geometry
         * that was walked any less real, and welding one leg to the next is safe on it.
         * This is the question a map with no navigation has to answer NO to, which a
         * "did it fail" test cannot -- such a map fails nothing, it just answers every
         * query with a line.
         */
        bool UsedGeometry() const
        {
            return outcome == RouteOutcome::Routed || outcome == RouteOutcome::Partial;
        }

        /// The mover will arrive: routed the whole way, or deliberately going direct.
        bool WillArrive() const
        {
            return outcome == RouteOutcome::Routed || outcome == RouteOutcome::Direct;
        }

        /// Nothing usable came back.
        bool Failed() const { return outcome == RouteOutcome::Unroutable; }
    };
}
