#pragma once

// One tile's worth of baked navigation, in memory, and every question that can be
// answered without leaving it.
//
// This is the data model the baker fills and the server reads. It knows nothing about
// searching -- no open list, no heuristic, no route -- because the search crosses tiles
// and this does not. What lives here is: which surfaces exist under a cell, whether a
// given mover may stand on one, and which region of the tile it belongs to.
//
// == Storage ==
//
// Four dense planes, one entry per cell, held apart rather than interleaved. The search
// probes the area plane far more often than the rest -- most visits end at "this mover
// may not be here" -- and separate planes keep that scan in a quarter of the cache
// lines an array of records would touch.
//
// Cells with more than one walkable surface -- the floors of a building, a bridge over
// a road -- keep their lowest surface in the dense planes and the rest in a sorted side
// table, flagged with CELL_STACKED. Interiors are a small fraction of any map, so the
// dense planes stay a fixed 1.5 MB per tile and stacking costs only where it exists.
//
// == Regions ==
//
// A region is a connected component of the tile's walkable surfaces: any two cells in
// one region are reachable from each other WITHOUT leaving the tile. That is what makes
// the coarse search possible -- a route across ten tiles is a walk over a few dozen
// regions rather than a million cells -- and it is why the fine search never has to
// look further than the tile it is refining.
//
// Regions are computed for the most permissive mover, so a region is an upper bound on
// connectivity: two cells in one region MAY be mutually reachable, and the fine search
// is what proves it for a particular mover. A region boundary, by contrast, is a hard
// fact -- nothing crosses it without a portal -- which is the direction an admissible
// search needs it to be exact in.

#include "nav/NavArea.hpp"
#include "nav/NavGrid.hpp"

#include <cstdint>
#include <vector>

namespace Nav
{
    /// A walkable surface above the lowest one in its cell.
    struct StackedLayer
    {
        uint32_t cell = 0;        ///< index into a tile's dense planes
        uint16_t z = 0;
        uint16_t region = 0;
        uint8_t area = 0;         ///< packed area + flags
        uint8_t clearance = 0;
    };

    /// One walkable surface, resolved. What a search actually stands on.
    struct Surface
    {
        float z = 0.0f;
        uint16_t region = 0;
        uint8_t area = 0;         ///< packed area + flags
        uint8_t clearance = 0;

        /// The index of this surface within its cell: 0 is the dense plane, and
        /// anything above indexes the stacked table. Carried so a search can name the
        /// node it is standing on without re-resolving it.
        uint16_t layer = 0;

        bool Valid() const { return Nav::Walkable(area); }
    };

    /// The node a search occupies: a cell, and which of its surfaces.
    struct NavNode
    {
        int32_t cellX = -1;
        int32_t cellY = -1;
        uint16_t layer = 0;

        bool operator==(const NavNode& o) const
        {
            return cellX == o.cellX && cellY == o.cellY && layer == o.layer;
        }
    };

    /**
     * @brief What the bake assumed about the mover, carried in the tile.
     *
     * Not decoration. The runtime has to JOIN two tiles at their shared border, and
     * whether a step across that border is a step or a fall is decided by the same
     * climb limit the baker used to decide it everywhere else. Recomputing it from a
     * server config would let the two disagree exactly at the seam, which is the one
     * place a disagreement produces a route that walks off a ledge.
     */
    struct TileParams
    {
        float agentHeight = 2.1f;
        float maxClimb = 1.0f;
        float maxSlopeDeg = 55.0f;
        float swimDepth = 1.5f;
    };

    /**
     * @brief A run of walkable cells along one edge of the tile: a way in or out.
     *
     * The unit the coarse search moves between. Note what defines one: only this tile's
     * own cells. A gateway knows nothing about the tile on the other side of the border,
     * which is what makes it safe to bake -- re-bake one map alone and its gateways
     * change, but no OTHER tile's file referred to them, so nothing is left pointing at
     * a region index that has since been renumbered. What joins two gateways across a
     * border is matched by the runtime store when both tiles are resident.
     *
     * A gateway is maximal: consecutive border cells of the same region, each connected
     * to the next, are one gateway. A doorway is one; a hundred-yard open border is also
     * one, and the fine search is what picks the point along it.
     */
    struct Gateway
    {
        /// Which edge of the tile. See NavTile::SIDE_*.
        uint8_t side = 0;

        /// The run, as local indices along the edge's varying axis, inclusive.
        uint16_t first = 0;
        uint16_t last = 0;

        uint16_t region = 0;

        /// Height at each end of the run, so a match across the border can test the
        /// step without re-reading cells.
        float firstZ = 0.0f;
        float lastZ = 0.0f;

        /// The narrowest clearance along the run, in yards. A mover wider than this
        /// cannot use the gateway even where the regions either side suit him.
        float width = 0.0f;

        /// World position of the run's midpoint, on this side of the border.
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /**
     * @brief A connected component of one tile's walkable surfaces.
     *
     * A region says nothing about its neighbours, and that is deliberate: what joins
     * two tiles is computed by the runtime store when BOTH are resident, never baked.
     * A baked crossing would name a region index in a file that may have been rebuilt
     * since, and a stale index is a route through a wall. See NavStore.
     */
    struct Region
    {
        uint32_t cellCount = 0;
        float minZ = 0.0f;
        float maxZ = 0.0f;

        /// Every area seen in this region, as AreaBit() bits. A mover whose permissions
        /// share no bit with this cannot be anywhere in the region, and the coarse
        /// search drops it without touching a cell.
        uint16_t areas = 0;

        /// The widest clearance anywhere in the region. A mover too wide for this has
        /// no cell here he fits on.
        float maxClearance = 0.0f;
    };

    /**
     * @brief One baked tile: the dense planes, the stacked surfaces, the regions.
     *
     * Immutable once built. The server holds them by shared_ptr and hands the same
     * instance to every query on that map, so nothing here may be mutated after the
     * loader publishes it.
     */
    class NavTile
    {
        public:
            /// Which edge of the tile a gateway sits on. Cell indices grow as world
            /// coordinates FALL, so LOW_X is the edge at the LARGEST world x. Naming
            /// them by the index rather than by the compass keeps the stitching honest.
            enum Side : uint8_t
            {
                SIDE_LOW_X = 0,    ///< local x == 0
                SIDE_HIGH_X = 1,   ///< local x == CELLS_PER_TILE - 1
                SIDE_LOW_Y = 2,    ///< local y == 0
                SIDE_HIGH_Y = 3,   ///< local y == CELLS_PER_TILE - 1

                SIDE_COUNT = 4
            };

            /// The cost matrix entry for a pair of gateways that cannot reach each
            /// other without leaving the tile.
            static constexpr float UNREACHABLE = 1e30f;

            /// The side of the neighbouring tile that faces a given side of this one.
            static uint8_t FacingSide(uint8_t side)
            {
                switch (side)
                {
                    case SIDE_LOW_X:  return SIDE_HIGH_X;
                    case SIDE_HIGH_X: return SIDE_LOW_X;
                    case SIDE_LOW_Y:  return SIDE_HIGH_Y;
                    default:          return SIDE_LOW_Y;
                }
            }

            /// Which side faces the neighbour at the given tile offset, or SIDE_COUNT
            /// when the offset is not one of the four orthogonal neighbours.
            static uint8_t SideTowards(int deltaX, int deltaY)
            {
                if (deltaY == 0 && deltaX == -1) { return SIDE_LOW_X; }
                if (deltaY == 0 && deltaX == 1)  { return SIDE_HIGH_X; }
                if (deltaX == 0 && deltaY == -1) { return SIDE_LOW_Y; }
                if (deltaX == 0 && deltaY == 1)  { return SIDE_HIGH_Y; }
                return SIDE_COUNT;
            }

            /// The in-tile cell index of a position along one border.
            static int BorderCell(uint8_t side, int position)
            {
                switch (side)
                {
                    case SIDE_LOW_X:
                        return position;
                    case SIDE_HIGH_X:
                        return (CELLS_PER_TILE - 1) * CELLS_PER_TILE + position;
                    case SIDE_LOW_Y:
                        return position * CELLS_PER_TILE;
                    default:
                        return position * CELLS_PER_TILE + (CELLS_PER_TILE - 1);
                }
            }

            /// Where along its border a cell sits, given which border it is on.
            static int BorderPosition(uint8_t side, int inTile)
            {
                const bool varyY = side == SIDE_LOW_X || side == SIDE_HIGH_X;
                return varyY ? inTile % CELLS_PER_TILE : inTile / CELLS_PER_TILE;
            }

            NavTile() = default;

            /// Allocate the dense planes for a tile at (tx, ty), all cells blocked.
            void Reset(int tileX, int tileY, float baseZ);

            int TileX() const { return m_tileX; }
            int TileY() const { return m_tileY; }
            float BaseZ() const { return m_baseZ; }

            const TileParams& Params() const { return m_params; }
            void SetParams(const TileParams& params) { m_params = params; }

            bool Empty() const { return m_regions.empty(); }

            // ---- dense plane access, by in-tile index -------------------------------

            uint8_t AreaAt(int inTile) const { return m_area[size_t(inTile)]; }
            uint8_t ClearanceAt(int inTile) const { return m_clearance[size_t(inTile)]; }
            uint16_t RegionAt(int inTile) const { return m_region[size_t(inTile)]; }
            float HeightAt(int inTile) const
            {
                return RestoreZ(m_z[size_t(inTile)], m_baseZ);
            }

            // ---- surfaces -----------------------------------------------------------

            /**
             * @brief Every walkable surface under one cell, lowest first.
             *
             * At most a handful even in the deepest interior. Returned by value into a
             * caller-owned buffer rather than allocated, because the search calls this
             * once per neighbour visit and an allocation there would dominate it.
             *
             * @param inTile Cell index within this tile.
             * @param out    Filled with the surfaces; cleared first.
             */
            void SurfacesAt(int inTile, std::vector<Surface>& out) const;

            /// One surface by its layer index, or an invalid Surface when there is none.
            Surface SurfaceAt(int inTile, uint16_t layer) const;

            /**
             * @brief The surface a body at height `z` is standing on.
             *
             * The nearest surface within `tolerance` yards, preferring one at or below
             * the body: a unit is on the floor it is above, not the ceiling it is under.
             * Returns an invalid Surface when nothing qualifies.
             */
            Surface SurfaceUnder(int inTile, float z, float tolerance) const;

            // ---- regions and portals ------------------------------------------------

            const std::vector<Region>& Regions() const { return m_regions; }

            const Region* RegionByIndex(uint16_t index) const
            {
                return index < m_regions.size() ? &m_regions[index] : nullptr;
            }

            const std::vector<Gateway>& Gateways() const { return m_gateways; }

            /**
             * @brief What it really costs to walk from one gateway of this tile to
             *        another, in yards, without leaving the tile.
             *
             * A measured distance, not an estimate: the baker runs the search for
             * every pair. That is the difference between a coarse search that is
             * merely admissible and one that is right. A straight line between two
             * gateways of a horseshoe-shaped region is half the true distance, and a
             * coarse search believing it picks the wrong way round the horseshoe --
             * legally, since the answer stays a valid path, but visibly.
             *
             * UNREACHABLE when the two do not connect inside this tile. That happens:
             * a tile can hold two regions that only join by going round through the
             * next tile.
             */
            float GatewayCost(size_t from, size_t to) const
            {
                const size_t n = m_gateways.size();
                if (from >= n || to >= n || m_gatewayCost.size() != n * n)
                {
                    return UNREACHABLE;
                }
                return m_gatewayCost[from * n + to];
            }

            // ---- construction, for the baker and the loader --------------------------

            void SetCell(int inTile, uint16_t z, uint8_t area, uint8_t clearance,
                         uint16_t region);
            void AddStacked(const StackedLayer& layer);
            void SortStacked();

            std::vector<Region>& MutableRegions() { return m_regions; }
            std::vector<Gateway>& MutableGateways() { return m_gateways; }
            std::vector<float>& MutableGatewayCost() { return m_gatewayCost; }
            const std::vector<float>& GatewayCostMatrix() const
            {
                return m_gatewayCost;
            }

            const std::vector<uint16_t>& ZPlane() const { return m_z; }
            const std::vector<uint8_t>& AreaPlane() const { return m_area; }
            const std::vector<uint8_t>& ClearancePlane() const { return m_clearance; }
            const std::vector<uint16_t>& RegionPlane() const { return m_region; }
            const std::vector<StackedLayer>& Stacked() const { return m_stacked; }

            std::vector<uint16_t>& MutableZPlane() { return m_z; }
            std::vector<uint8_t>& MutableAreaPlane() { return m_area; }
            std::vector<uint8_t>& MutableClearancePlane() { return m_clearance; }
            std::vector<uint16_t>& MutableRegionPlane() { return m_region; }
            std::vector<StackedLayer>& MutableStacked() { return m_stacked; }

            void SetTileIndex(int tileX, int tileY)
            {
                m_tileX = tileX;
                m_tileY = tileY;
            }

            void SetBaseZ(float baseZ) { m_baseZ = baseZ; }

            /// Bytes this tile occupies, for the cache's own accounting.
            size_t Footprint() const;

        private:
            /// Half-open range of the stacked table belonging to one cell.
            void StackedRange(int inTile, size_t& first, size_t& last) const;

            int m_tileX = -1;
            int m_tileY = -1;
            float m_baseZ = 0.0f;
            TileParams m_params;

            std::vector<uint16_t> m_z;
            std::vector<uint8_t> m_area;
            std::vector<uint8_t> m_clearance;
            std::vector<uint16_t> m_region;

            /// Sorted by cell, then by height. Binary-searched, and only when the dense
            /// cell says CELL_STACKED.
            std::vector<StackedLayer> m_stacked;

            std::vector<Region> m_regions;

            std::vector<Gateway> m_gateways;

            /// Row-major, gateways x gateways, symmetric. Small: a tile has tens of
            /// gateways, not thousands, because a gateway is a whole RUN of border
            /// cells rather than one of them.
            std::vector<float> m_gatewayCost;
    };
}
