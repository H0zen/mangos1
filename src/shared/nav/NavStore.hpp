#pragma once

// Which tiles of one map are in memory, and how they join up.
//
// Two jobs, and they belong together because the second is a consequence of the first.
//
// == Residency ==
//
// Tiles arrive and leave with the map's grids, so the set of navigable ground changes
// while the server runs. Every query goes through here, and a query that reaches a tile
// nobody has loaded is answered "no ground", never guessed at.
//
// == Stitching ==
//
// A baked tile knows only its own cells. What joins two tiles is computed HERE, when
// both are resident, by matching their border cells against each other. Nothing about
// the join is baked, and that is the whole reason it can be trusted: a baked crossing
// would name a region index inside the neighbour's file, and re-baking one map alone
// renumbers its regions, leaving every neighbour pointing somewhere plausible and
// wrong. A crossing computed from two files that are both in memory right now cannot be
// stale.
//
// The cost of doing it at runtime is one scan of 512 border cells per tile pair, once,
// when a tile loads. The cost of doing it offline is a class of silent bug.

#include "nav/NavTile.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Nav
{
    /// A tile, addressed the way the map's grid addresses it.
    struct TileKey
    {
        int16_t x = 0;
        int16_t y = 0;

        bool operator==(const TileKey& o) const { return x == o.x && y == o.y; }
    };

    /// One gateway of one tile: a node of the coarse graph.
    struct GateRef
    {
        int16_t tileX = 0;
        int16_t tileY = 0;
        uint16_t gate = 0;

        bool operator==(const GateRef& o) const
        {
            return tileX == o.tileX && tileY == o.tileY && gate == o.gate;
        }
    };

    /// A way from one tile's gateway into the neighbouring tile's.
    struct Crossing
    {
        GateRef to;

        /**
         * @brief The WIDEST clearance among the matched cells, in yards.
         *
         * Widest and not narrowest, deliberately: a crossing is a run of border cells,
         * and a mover needs ONE of them to fit through, not all of them. Taking the
         * narrowest would refuse a devilsaur a gate it walks through every day because
         * the same gateway also touches a crack in the wall beside it.
         *
         * The cost of that choice is that the fine search must then actually reach a
         * cell that fits, rather than whichever cell it arrived at -- see
         * Router's StepAcross, which walks along the border to find one.
         */
        float width = 0.0f;

        /// What the step across the border costs, in yards. One cell: the crossing is
        /// a shared edge, not a journey.
        float cost = 0.0f;
    };

    /// Packs a gateway reference into one integer, for the adjacency map's key.
    inline uint64_t PackGate(const GateRef& ref)
    {
        return (uint64_t(uint16_t(ref.tileX)) << 32) |
               (uint64_t(uint16_t(ref.tileY)) << 16) | uint64_t(ref.gate);
    }

    inline GateRef UnpackGate(uint64_t packed)
    {
        GateRef ref;
        ref.tileX = int16_t(uint16_t(packed >> 32));
        ref.tileY = int16_t(uint16_t((packed >> 16) & 0xFFFFu));
        ref.gate = uint16_t(packed & 0xFFFFu);
        return ref;
    }

    /**
     * @brief The resident navigation for one map.
     *
     * SHARED BETWEEN INSTANCES, and therefore between threads. That is not a design
     * choice, it is a fact about the server: TerrainInfo is refcounted per map id
     * (sTerrainMgr.LoadTerrain), every instance of Karazhan holds the same one, and
     * each instance is updated on its own MapUpdater worker. Two workers loading
     * different grids of the same map, or one loading while another routes, reach this
     * object at the same moment.
     *
     * So two things are true here and nowhere else in this module:
     *
     *  - the tables are guarded by a mutex. It is held only across the lookup, never
     *    across a search;
     *  - a tile is handed out as a shared_ptr, never as a raw pointer. A search that
     *    held a raw one would be reading freed memory the instant another worker
     *    unloaded that grid -- and it would read it for the whole of the search, which
     *    is the longest window in the subsystem.
     */
    class NavStore
    {
        public:
            explicit NavStore(uint32_t mapId) : m_mapId(mapId) {}

            uint32_t MapId() const { return m_mapId; }

            /**
             * @brief Bring one tile in, and join it to whichever neighbours are here.
             * @return False when the file is missing, stale or corrupt -- all of which
             *         mean the same thing to every query: no ground in that tile.
             */
            bool LoadTile(int tileX, int tileY);

            /// Drop one tile, and every crossing that mentioned it.
            void UnloadTile(int tileX, int tileY);

            void Clear();

            bool IsResident(int tileX, int tileY) const;

            /// The tile, or nullptr. Never a half-loaded one, and never one another
            /// thread can free while the caller still holds it.
            std::shared_ptr<const NavTile> TileAt(int tileX, int tileY) const;

            /// The tile a global cell falls in.
            std::shared_ptr<const NavTile> TileOf(const CellRef& cell) const;

            /// Every tile currently in memory. A snapshot: callers unload while walking.
            void ResidentTiles(std::vector<TileKey>& out) const;

            size_t ResidentCount() const;
            size_t Footprint() const;

            /// The crossings leading out of one gateway. Empty when the neighbour on
            /// that side is not resident, which is the correct answer and not an error.
            ///
            /// By value: a reference into the table would outlive the lock that made it
            /// safe to read, and a gateway has a handful of crossings, not thousands.
            std::vector<Crossing> CrossingsOf(const GateRef& from) const;

            /**
             * @brief The walkable surface a position is standing on, if any.
             *
             * @param x,y,z    World position.
             * @param tolerance How far above or below the position a surface may be and
             *                  still be the one it is standing on, in yards.
             * @param cell     Filled with the cell the position falls in.
             * @param surface  Filled with the surface; invalid when there is none.
             * @return True when a surface was found.
             */
            bool SurfaceAt(float x, float y, float z, float tolerance, CellRef& cell,
                           Surface& surface) const;

        private:
            struct KeyHash
            {
                size_t operator()(uint32_t k) const { return size_t(k); }
            };

            static uint32_t Pack(int tileX, int tileY)
            {
                return (uint32_t(uint16_t(int16_t(tileX))) << 16) |
                       uint32_t(uint16_t(int16_t(tileY)));
            }

            /// Match one tile's border against the neighbour's, both ways.
            /// The caller already holds m_mutex.
            void StitchLocked(int tileX, int tileY, int neighbourX, int neighbourY);

            /// Forget every crossing that starts or ends inside one tile.
            /// The caller already holds m_mutex.
            void UnstitchLocked(int tileX, int tileY);

            /// Lookup without taking the lock, for the methods that already hold it.
            std::shared_ptr<const NavTile> TileAtLocked(int tileX, int tileY) const;

            /// The gateway of `tile` covering a border position in a given region.
            static int FindGateway(const NavTile& tile, uint8_t side, int position,
                                   uint16_t region);

            uint32_t m_mapId = 0;

            /// Guards both tables. Held across a lookup, never across a search.
            mutable std::mutex m_mutex;

            std::unordered_map<uint32_t, std::shared_ptr<const NavTile>> m_tiles;

            /// Gateway -> the crossings out of it. Rebuilt as tiles come and go.
            std::unordered_map<uint64_t, std::vector<Crossing>> m_crossings;
    };

    /**
     * @brief The stores, one per map.
     *
     * A registry and not a singleton with logic in it: the only thing it owns is the
     * map from a map id to that map's resident tiles.
     */
    class NavStores
    {
        public:
            static NavStores& Instance();

            /// The store for a map, created on first ask.
            NavStore& For(uint32_t mapId);

            /// The store for a map, or nullptr when nothing has ever been loaded for it.
            const NavStore* Find(uint32_t mapId) const;

            void Drop(uint32_t mapId);
            void Clear();

            size_t MapCount() const { return m_maps.size(); }
            size_t TileCount() const;

        private:
            NavStores() = default;

            /// Guards the map table only. The stores themselves lock their own tables;
            /// this one exists because two workers can be the first to touch two
            /// different maps at the same moment, and both would insert.
            mutable std::mutex m_mutex;

            std::unordered_map<uint32_t, std::unique_ptr<NavStore>> m_maps;
    };
}
