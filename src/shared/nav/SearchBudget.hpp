#pragma once

// How a route is turned into points, and what one request may spend producing them.

#include <algorithm>
#include <cstdint>

namespace Nav
{
    /**
     * @brief Points a route may contain.
     *
     * Was 74, on the reasoning that 74 * 4 yards is 296 and that is far past evade
     * range. Two measurements say otherwise.
     *
     * The client accepts more: across four retail captures the longest monster-move
     * carried 93 points, so 74 was never a limit the protocol imposed -- it was one this
     * server chose, and then forgot it had chosen.
     *
     * And it bit. On the live server, eleven paths in one session stopped because they
     * ran out of points, several of them still ninety yards or more from their goal.
     * Those are not creatures wandering too far; they are ordinary routes through
     * geometry that needs more corners than the budget allowed.
     *
     * 93 is what retail was seen to send. Going past it would be inventing headroom
     * nobody has observed the client using.
     */
    constexpr uint32_t MAX_POINTS = 93;

    /// Distance the point emitter advances along the route per step, in yards. Only a
    /// ceiling: the emitter puts a point where the route TURNS, and a straight leg of
    /// two hundred yards is two points, not fifty.
    constexpr float SMOOTH_STEP = 4.0f;

    /// How close to a target counts as having reached it, in yards.
    constexpr float SMOOTH_SLOP = 0.3f;

    /// How far above the surface an emitted point sits, in yards. Clearance, so the
    /// point is not exactly on the ground it was projected onto.
    constexpr float GROUND_CLEARANCE = 0.5f;

    /// How far BELOW a liquid surface a swimmer sits, in yards. The same two yards
    /// TerrainInfo seats a swimmer at, so the router and the terrain do not hold two
    /// different opinions about where the water puts a body.
    constexpr float SWIM_SEAT_DEPTH = 2.0f;

    /// The default ceiling on a routed path, in yards: the whole point budget spent.
    constexpr float DEFAULT_LENGTH = float(MAX_POINTS) * SMOOTH_STEP;

    /**
     * @brief What one routing request may spend.
     *
     * A value passed to the request, where it used to be a setter that mutated the
     * router and stayed mutated. That mattered as soon as a router outlived a single
     * leg: the limit was STICKY, so an unlimited request issued after a capped one -- a
     * chase after a flee -- silently inherited the cap, and the only defence was for
     * every caller to remember to re-apply a default it should never have had to know
     * about.
     */
    struct SearchBudget
    {
        /// Points the produced path may contain, never above Nav::MAX_POINTS.
        uint32_t points = MAX_POINTS;

        /**
         * @brief Cells the fine search may expand before it gives up.
         *
         * Distinct from the point budget, and it has to be: how far a creature is
         * ALLOWED to chase is a rule of the game, expressed in yards, while how much
         * work a search may do before it admits defeat is a property of the machine.
         * Conflating them is how a creature in a dense city was told there was no path
         * to a room it could plainly walk to.
         *
         * The coarse search over gateways is not bounded by this. It cannot run away:
         * a map has tens of gateways per tile, not thousands of cells.
         */
        uint32_t cells = 60000;

        /**
         * @brief How long the route may be, in yards. Zero means no cap.
         *
         * The point count is NOT a length, and treating it as one was wrong even before
         * the emitter changed. `points = yards / 4` assumed every point stood for four
         * yards of walking; a straight two-hundred-yard run is two points and sailed
         * past a ten-yard cap, while a ten-yard scramble round five corners was cut
         * short of a limit it never reached. Flee and confused movement did not have the
         * ceiling they were written to have.
         *
         * Now the length is measured, in yards, over the points actually emitted.
         */
        float maxLength = 0.0f;

        /// When true, a route longer than maxLength is refused rather than clipped.
        /// Wander wants this (a 160-yard coastal detour is not a 13-yard hop). Flee
        /// wants the clip: run this far and stop.
        bool rejectIfLonger = false;

        /**
         * @brief A budget for a route of at most @p yards.
         *
         * A non-positive length means "no rule of the game applies here", which is the
         * full budget rather than an empty one -- the caller is declining to cap the
         * route, not asking for a path of no points.
         */
        static SearchBudget ForLength(float yards)
        {
            SearchBudget budget;
            if (yards > 0.0f)
            {
                budget.maxLength = yards;

                // The point ceiling still applies, but as what it actually is: the size
                // of the buffer the client will accept, not a distance.
                budget.points = MAX_POINTS;
            }
            return budget;
        }

        /// Like ForLength, but a route that would need clipping is not a route.
        static SearchBudget Within(float yards)
        {
            SearchBudget budget = ForLength(yards);
            budget.rejectIfLonger = true;
            return budget;
        }
    };
}
