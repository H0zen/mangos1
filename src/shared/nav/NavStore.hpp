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

#include "nav/NavMesh.hpp"
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

            /**
             * @brief Note that a search needed a tile nobody had loaded.
             *
             * Routing is bounded by RESIDENCY, and residency follows the map's grids --
             * which follow players and active objects. A chase across two tiles of
             * empty countryside therefore failed at a tile in the middle that nothing
             * happened to be standing in, and the mover fell back to a straight line
             * through whatever was there.
             *
             * The tile is not loaded here. Reading one is a megabyte of file, and this
             * is called from the middle of a search on the map's own thread; stalling
             * the tick to widen a route is a worse bargain than the route. It is
             * remembered, PumpWanted brings a few in per tick, and the next attempt --
             * a chase replans every few hundred milliseconds -- has them.
             *
             * Const because a search is const. The want list is what changes, and it is
             * guarded like everything else here.
             */
            void Want(int tileX, int tileY) const;

            /// Every tile the straight line between two points passes through, wanted.
            void WantAlong(float fromX, float fromY, float toX, float toY) const;

            /**
             * @brief Bring in a few wanted tiles, and drop unpinned ones over the cap.
             *
             * Called from the map's own update, where a few milliseconds of file
             * reading is affordable and a search is not running.
             *
             * @param maxLoads How many to read this call. Small on purpose.
             * @return How many were loaded.
             */
            size_t PumpWanted(size_t maxLoads = 2);

            /// Tiles the pump may hold that no grid pins. Beyond this the least
            /// recently used are dropped, so a creature routing across a continent
            /// cannot pull the whole map into memory.
            static constexpr size_t MAX_UNPINNED = 48;

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

            /**
             * @brief The convex-area view of a resident tile, built on first ask.
             *
             * The file still carries cells; this is derived from them once and kept for
             * as long as the tile is resident. Derivation is a pass over a quarter of a
             * million cells, so it happens OUTSIDE the store's lock: the tile is taken
             * under the lock, released, built, and published back if the tile is still
             * there. Two threads racing the same tile build it twice and one of them
             * throws its copy away, which costs a pass and no correctness -- holding the
             * lock across the build would stall every other search on the map instead.
             *
             * @return The mesh, or nullptr when the tile is not resident.
             */
            std::shared_ptr<const TileMesh> MeshOf(int tileX, int tileY) const;

            /**
             * @brief The mesh crossings leading out of one rectangle of one tile.
             *
             * What replaces `CrossingsOf` for a search that walks areas instead of
             * gateways. Matched from the two tiles' rim runs when both are resident, and
             * forgotten when either leaves -- so a crossing never outlives the geometry
             * that justified it, and no file ever recorded a number about its neighbour.
             *
             * By value for the same reason as the gateway version: a reference into the
             * table would outlive the lock that made it safe to read.
             */
            std::vector<MeshCrossing> MeshCrossingsOf(int tileX, int tileY,
                                                      uint32_t rect) const;

        private:
            /// Match one tile's rim against every resident orthogonal neighbour, and
            /// record the crossings both ways. The caller already holds m_mutex.
            ///
            /// Const, like the mesh it is derived from: a crossing is not something the
            /// store CONTAINS, it is what two resident meshes imply, and it is dropped
            /// and rebuilt as they come and go. A lookup that fills it has not changed
            /// what the store holds any more than one that fills `touched` has.
            void StitchMeshLocked(int tileX, int tileY) const;

            /// Forget every mesh crossing that starts or ends inside one tile.
            /// The caller already holds m_mutex.
            void UnstitchMeshLocked(int tileX, int tileY) const;

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

            /// The one loader. `pinned` says whether a GRID is asking, which decides
            /// whether the cap may later evict it.
            bool LoadTileInternal(int tileX, int tileY, bool pinned);

            /// Drop unpinned tiles beyond MAX_UNPINNED, least recently used first.
            void EvictUnpinned();


            /// The gateway of `tile` covering a border position in a given region.
            static int FindGateway(const NavTile& tile, uint8_t side, int position,
                                   uint16_t region);

            /// A resident tile, and whether a GRID is holding it here.
            struct Resident
            {
                std::shared_ptr<const NavTile> tile;

                /// True when TerrainInfo loaded it with a grid. Those come and go with
                /// GridMap and are never evicted by the cap; only the pump's own are.
                bool pinned = false;

                /// Bumped on every lookup, for the cap's eviction order. Mutable
                /// because a LOOKUP is const and still counts as use -- that is the
                /// whole point of tracking it.
                mutable uint64_t touched = 0;

                /// The convex-area view, derived on first ask and dropped with the tile.
                /// Mutable for the same reason as `touched`: it is a cache, and filling
                /// a cache is not a change to what the store contains.
                mutable std::shared_ptr<const TileMesh> mesh;
            };

            uint32_t m_mapId = 0;

            /// Guards both tables. Held across a lookup, never across a search.
            mutable std::mutex m_mutex;

            std::unordered_map<uint32_t, Resident> m_tiles;

            /// Wanted but not resident. Bounded: a search that wants a hundred tiles is
            /// a search that should fail, not one that should page in a continent.
            mutable std::vector<uint32_t> m_wanted;

            mutable uint64_t m_clock = 0;

            /// Gateway -> the crossings out of it. Rebuilt as tiles come and go.
            std::unordered_map<uint64_t, std::vector<Crossing>> m_crossings;

            /// (tile, rectangle) -> the mesh crossings out of it. The same idea one
            /// layer up, and what the gateway table becomes when the areas are the
            /// structure. Keyed by the tile packed with the rectangle index, so a tile
            /// leaving can drop all of its own without touching anyone else's.
            ///
            /// Mutable for the same reason as the mesh it is matched from: derived,
            /// droppable, and filled by a lookup that is const because a search is const.
            mutable std::unordered_map<uint64_t, std::vector<MeshCrossing>>
                m_meshCrossings;

            static uint64_t MeshKey(int tileX, int tileY, uint32_t rect)
            {
                return (static_cast<uint64_t>(Pack(tileX, tileY)) << 32) |
                       static_cast<uint64_t>(rect);
            }
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
