#pragma once

// Driving the navigation bake over every map the tile directory holds.
//
// Almost nothing happens here. Nav::BuildNavTile does the work, and it does it through
// the same FusedTerrain the server collides against, so this file is only: find the
// maps, hand out the tiles, write the files, count them.
//
// That is the measure of the rewrite. The generator this replaces spent twelve hundred
// lines assembling triangle soups, stitching neighbour geometry into them, splitting
// tiles into sub-tiles to bound the rasteriser's memory, and translating between three
// coordinate conventions -- all of it in service of feeding a mesh generator that wanted
// geometry rather than answers. Asking the terrain what is under a point needs none of
// it.

#include "nav/NavBuilder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Nav
{
    struct BakeConfig
    {
        BuildParams params;

        /// The hand-authored links file, or empty for none. See LoadOffMesh.
        std::string offMeshFile;

        /// Worker threads. Zero asks the hardware.
        ///
        /// Each worker gets its OWN view of the terrain. The terrain cache loads tiles
        /// lazily and is not safe to share across threads, and giving each worker a
        /// whole tile to bake -- rather than splitting one tile's rows -- means no two
        /// of them ever touch the same cache.
        int threads = 0;
    };

    class NavBaker
    {
        public:
            NavBaker(std::string tileDir, std::string outDir, BakeConfig cfg = {});

            /// Live progress WITHIN a map: `done` of `total` tiles started. Called only
            /// from the main thread, so it needs no locking of its own.
            using ProgressFn = void (*)(void* context, uint32_t mapId,
                                        const char* mapName, size_t done, size_t total);
            void SetProgress(ProgressFn fn, void* context);

            /// One durable line per finished map, for a piped log where the moving
            /// header renders nothing.
            using MapDoneFn = void (*)(void* context, uint32_t mapId,
                                       const char* mapName, int written, size_t total);
            void SetMapDone(MapDoneFn fn);

            /// Bakes every map that has tiles, or only `mapFilter` when >= 0.
            /// @return The number of .nav files written, or -1 on a fatal error.
            int BakeAll(long mapFilter = -1);

        private:
            int BakeMap(uint32_t mapId, const std::string& label,
                        const std::vector<std::pair<int, int>>& grids,
                        const std::vector<LinkSpec>& links);

            std::string m_tileDir;
            std::string m_outDir;
            BakeConfig m_cfg;
            ProgressFn m_progress = nullptr;
            void* m_progressContext = nullptr;
            MapDoneFn m_mapDone = nullptr;
    };
}
