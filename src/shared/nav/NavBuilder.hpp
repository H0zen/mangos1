#pragma once

// Turning baked terrain into baked navigation, one tile at a time.
//
// The input is the FUSED TERRAIN -- the same object the server collides against at
// runtime -- and not the client's archives. That is the whole reason this is worth
// rewriting: the surface a creature is routed over is by construction the surface it
// will be stopped by, because one gather answers both. A separate rasteriser reading
// the original geometry can only ever AGREE with the collision engine, and agreement is
// a thing that decays.
//
// == Why sampling and not rasterising ==
//
// The mesh generator this replaces rasterised triangles into voxels, grew regions out
// of the voxels, walked contours round the regions and triangulated the contours -- four
// stages, each with its own failure mode, and the last three exist only to get back to
// a polygon. Nothing downstream wanted a polygon; the router wanted to know where a
// creature may stand.
//
// So ask that directly. One vertical gather per cell answers, in one step, every
// question the four stages were approximating: what floors are here, what is above each
// of them, what liquid covers them. It is a ray query against a BVH that is already
// built, and it cannot disagree with the runtime because it IS the runtime's query.
//
// == The margin ==
//
// Cells are sampled over a MARGIN outside the tile as well. Without it every quantity
// computed from neighbours -- the slope of a border cell, the room around it -- would
// be measured against cells that simply are not there, and every tile would come out
// with a rim of steep, cramped, unusable ground exactly where tiles have to join. The
// margin is sampled, used, and cropped away.

#include "nav/NavTile.hpp"

#include <cstdint>

namespace world::terrain { class FusedTerrain; }

namespace Nav
{
    /**
     * @brief What the bake assumes about the creature it is baking for.
     *
     * Note what is NOT here: an agent radius. The mesh generator this replaces eroded
     * the walkable surface by one radius fixed at bake time, which is why a murloc and
     * a devilsaur were routed through identically sized doorways. Width is recorded per
     * cell instead -- see NavArea's clearance -- and compared against the mover's own
     * radius at query time, so one bake serves every creature in the game.
     */
    struct BuildParams
    {
        /// Headroom a mover needs to stand somewhere, in yards. A floor with less than
        /// this above it is a crawl space and is not walkable at all.
        float agentHeight = 2.1f;

        /// The tallest step up that is a step and not a fall, in yards. Decides which
        /// neighbouring cells are connected, so it decides what a region is.
        float maxClimb = 1.0f;

        /// Steepest ground a mover keeps its feet on, in degrees.
        float maxSlopeDeg = 55.0f;

        /// Ground steeper than this is recorded but priced as CELL_STEEP rather than
        /// refused, so a route may use a bank it would rather not.
        float steepSlopeDeg = 45.0f;

        /// Liquid deeper than this over a floor makes that floor a seabed and puts a
        /// separate swimmable surface at the liquid's top, in yards.
        float swimDepth = 1.5f;

        /// The vertical window each cell is gathered over, in yards. Wide enough for
        /// any 2.4.3 map, and it costs nothing: the gather is bounded by the geometry
        /// it crosses, not by the window.
        float zTop = 2200.0f;
        float zBottom = -2200.0f;

        /// Cells sampled outside the tile on every side, so quantities measured from
        /// neighbours are right up to the border. See the header comment.
        int margin = 8;
    };

    /**
     * @brief Build the navigation for one tile.
     *
     * @param terrain Fused terrain for the map, positioned by nothing -- it is asked
     *                for world coordinates and loads whatever tiles they fall in,
     *                including the neighbours the margin reaches into.
     * @param tileX   Tile index along world X, as world::terrain::TileIndex reports it.
     * @param tileY   Tile index along world Y.
     * @param params  What the bake assumes about the mover.
     * @param out     Filled with the result; untouched when the tile has no ground.
     * @return False when nothing walkable was found, in which case no file should be
     *         written: a tile of nothing and a missing tile mean the same thing to the
     *         runtime, and the missing one costs no disk.
     */
    bool BuildNavTile(const world::terrain::FusedTerrain& terrain, int tileX, int tileY,
                      const BuildParams& params, NavTile& out);
}
