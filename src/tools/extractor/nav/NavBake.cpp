#include "NavBake.hpp"

#include "nav/NavTileIO.hpp"
#include "terrain/FusedTerrain.hpp"
#include "terrain/TileSerializer.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace Nav
{
    namespace
    {
        std::mutex g_logMutex;

        /// Grids a map with no ADT terrain -- a lone WMO, a ship's hull -- covers.
        ///
        /// Read off the instances' own world bounds rather than assumed, because such a
        /// map has no grid of tiles to enumerate. A rectangle over the union can name a
        /// grid the geometry never reaches; that tile finds no ground and writes
        /// nothing, which is exactly what should happen.
        std::vector<std::pair<int, int>> GlobalWmoGrids(
            const world::terrain::TerrainTile& tile)
        {
            Geometry::Aabb box;
            for (const world::terrain::StaticInstance& inst : tile.instances)
            {
                if (inst.worldBounds.valid())
                {
                    box.expand(inst.worldBounds);
                }
            }

            if (!box.valid())
            {
                return {};
            }

            std::vector<std::pair<int, int>> grids;

            // Both axes fall as the index grows, so the high world corner gives the low
            // index. Getting this backwards produces an empty loop and a map that bakes
            // silently to nothing.
            const int xFirst = world::terrain::TileIndex(box.hi.x);
            const int xLast = world::terrain::TileIndex(box.lo.x);
            const int yFirst = world::terrain::TileIndex(box.hi.y);
            const int yLast = world::terrain::TileIndex(box.lo.y);

            for (int gx = std::max(0, xFirst); gx <= std::min(63, xLast); ++gx)
            {
                for (int gy = std::max(0, yFirst); gy <= std::min(63, yLast); ++gy)
                {
                    grids.emplace_back(gx, gy);
                }
            }

            return grids;
        }
    }

    NavBaker::NavBaker(std::string tileDir, std::string outDir, BakeConfig cfg)
        : m_tileDir(std::move(tileDir)), m_outDir(std::move(outDir)),
          m_cfg(std::move(cfg))
    {
    }

    void NavBaker::SetProgress(ProgressFn fn, void* context)
    {
        m_progress = fn;
        m_progressContext = context;
    }

    void NavBaker::SetMapDone(MapDoneFn fn) { m_mapDone = fn; }

    int NavBaker::BakeMap(uint32_t mapId, const std::string& label,
                          const std::vector<std::pair<int, int>>& grids)
    {
        if (grids.empty())
        {
            return 0;
        }

        unsigned workers = m_cfg.threads > 0 ? unsigned(m_cfg.threads)
                                             : std::thread::hardware_concurrency();
        if (workers == 0)
        {
            workers = 1;
        }
        workers = std::min<unsigned>(workers, unsigned(grids.size()));

        std::atomic<size_t> next{0};
        std::atomic<int> written{0};
        std::atomic<int> failures{0};

        auto report = [&](size_t started)
        {
            if (m_progress)
            {
                m_progress(m_progressContext, mapId, label.c_str(),
                           std::min(started, grids.size()), grids.size());
            }
        };

        auto worker = [&](bool isMain)
        {
            // One terrain view per worker. The cache loads lazily and is not shared.
            world::terrain::FusedTerrain terrain(mapId);
            NavTile tile;

            for (;;)
            {
                const size_t i = next.fetch_add(1);
                if (i >= grids.size())
                {
                    return;
                }

                const int gx = grids[i].first;
                const int gy = grids[i].second;

                if (BuildNavTile(terrain, gx, gy, m_cfg.params, tile))
                {
                    const std::string path =
                        m_outDir + "/" + NavTileFileName(mapId, gx, gy);

                    if (WriteNavTile(path, mapId, tile))
                    {
                        ++written;
                    }
                    else
                    {
                        // A half-written file is worse than none: the reader would
                        // refuse it, but only after the bake reported success.
                        std::error_code ec;
                        std::filesystem::remove(path, ec);

                        ++failures;
                        std::lock_guard<std::mutex> lock(g_logMutex);
                        std::fprintf(stderr,
                                     "nav: map %u tile %d,%d could not be written\n",
                                     mapId, gx, gy);
                    }
                }

                // Let the terrain cache drop what this tile pulled in. Without it a
                // worker that bakes a continent holds every tile it ever touched.
                terrain.Update(60000);

                if (isMain)
                {
                    report(next.load());
                }
            }
        };

        report(0);

        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (unsigned i = 1; i < workers; ++i)
        {
            pool.emplace_back(worker, false);
        }
        worker(true);
        for (std::thread& t : pool)
        {
            t.join();
        }
        report(grids.size());

        if (failures.load() != 0)
        {
            return -1;
        }

        return written.load();
    }

    int NavBaker::BakeAll(long mapFilter)
    {
        std::error_code ec;
        std::filesystem::create_directories(m_outDir, ec);
        if (ec)
        {
            return -1;
        }

        world::terrain::FusedTerrain::SetTileDir(m_tileDir);

        // Which maps and grids exist is read off the baked terrain tiles themselves, so
        // the navigation can only ever cover ground the collision engine also has. A map
        // is either an ADT grid (t_ tiles) or a single global WMO (a lone w_ tile).
        std::map<uint32_t, std::vector<std::pair<int, int>>> byMap;
        std::set<uint32_t> globalWmoMaps;

        for (const auto& entry : std::filesystem::directory_iterator(m_tileDir, ec))
        {
            const std::string leaf = entry.path().filename().string();
            unsigned mapId = 0;
            int gx = 0;
            int gy = 0;

            if (std::sscanf(leaf.c_str(), "t_%u_%d_%d.tile", &mapId, &gx, &gy) == 3)
            {
                if (mapFilter < 0 || uint32_t(mapFilter) == mapId)
                {
                    byMap[mapId].emplace_back(gx, gy);
                }
            }
            else if (std::sscanf(leaf.c_str(), "w_%u.tile", &mapId) == 1)
            {
                if (mapFilter < 0 || uint32_t(mapFilter) == mapId)
                {
                    globalWmoMaps.insert(mapId);
                }
            }
        }

        if (ec)
        {
            return -1;
        }

        const size_t mapCount = byMap.size() + globalWmoMaps.size();
        int total = 0;
        size_t done = 0;

        for (auto& entry : byMap)
        {
            char label[48];
            std::snprintf(label, sizeof(label), "map %u  [%zu/%zu]", entry.first,
                          done + 1, mapCount);

            const int count = BakeMap(entry.first, label, entry.second);
            if (count < 0)
            {
                return -1;
            }

            if (m_mapDone)
            {
                m_mapDone(m_progressContext, entry.first, label, count,
                          entry.second.size());
            }

            total += count;
            ++done;
        }

        for (uint32_t mapId : globalWmoMaps)
        {
            char label[48];
            std::snprintf(label, sizeof(label), "map %u  [%zu/%zu]", mapId, done + 1,
                          mapCount);

            std::shared_ptr<world::terrain::TerrainTile> tile =
                world::terrain::ReadTile(m_tileDir + "/" +
                                         world::terrain::GlobalWmoFileName(mapId));
            if (tile)
            {
                const std::vector<std::pair<int, int>> grids = GlobalWmoGrids(*tile);
                const int count = BakeMap(mapId, label, grids);
                if (count < 0)
                {
                    return -1;
                }

                if (m_mapDone)
                {
                    m_mapDone(m_progressContext, mapId, label, count, grids.size());
                }

                total += count;
            }

            ++done;
        }

        return total;
    }
}
