#include "nav/NavStore.hpp"

#include "nav/NavTileIO.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Nav
{
    // ---------------------------------------------------------------- NavStore ----

    bool NavStore::LoadTile(int tileX, int tileY)
    {
        const uint32_t key = Pack(tileX, tileY);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_tiles.find(key) != m_tiles.end())
            {
                return true;
            }
        }

        // Read OUTSIDE the lock. Parsing a tile is a megabyte of file and the slowest
        // thing this class does; holding the lock across it would stall every routing
        // query on the map behind one grid load.
        const std::string path =
            NavDir() + "/" + NavTileFileName(m_mapId, tileX, tileY);

        auto tile = std::make_shared<NavTile>();
        if (!ReadNavTile(path, m_mapId, *tile))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        // Re-checked: another worker may have loaded the same grid while this one was
        // reading it. Two instances of the same map load the same tiles, on their own
        // threads, and both call this.
        if (m_tiles.find(key) != m_tiles.end())
        {
            return true;
        }

        m_tiles.emplace(key, std::move(tile));

        // Join it to whichever of its four neighbours are already here. A neighbour
        // arriving later stitches from its own side, so the pair is joined exactly once
        // whichever order they load in.
        StitchLocked(tileX, tileY, tileX - 1, tileY);
        StitchLocked(tileX, tileY, tileX + 1, tileY);
        StitchLocked(tileX, tileY, tileX, tileY - 1);
        StitchLocked(tileX, tileY, tileX, tileY + 1);

        return true;
    }

    void NavStore::UnloadTile(int tileX, int tileY)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const uint32_t key = Pack(tileX, tileY);

        // Dropped from the table here, but the memory outlives this call for as long as
        // any search still holds its shared_ptr. That is the whole reason a lookup hands
        // one out instead of a raw pointer.
        if (m_tiles.erase(key) == 0)
        {
            return;
        }

        UnstitchLocked(tileX, tileY);
    }

    void NavStore::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tiles.clear();
        m_crossings.clear();
    }

    bool NavStore::IsResident(int tileX, int tileY) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tiles.find(Pack(tileX, tileY)) != m_tiles.end();
    }

    std::shared_ptr<const NavTile> NavStore::TileAtLocked(int tileX, int tileY) const
    {
        const auto it = m_tiles.find(Pack(tileX, tileY));
        return it == m_tiles.end() ? nullptr : it->second;
    }

    std::shared_ptr<const NavTile> NavStore::TileAt(int tileX, int tileY) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return TileAtLocked(tileX, tileY);
    }

    std::shared_ptr<const NavTile> NavStore::TileOf(const CellRef& cell) const
    {
        if (!cell.Valid())
        {
            return nullptr;
        }
        return TileAt(cell.TileX(), cell.TileY());
    }

    void NavStore::ResidentTiles(std::vector<TileKey>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.clear();
        out.reserve(m_tiles.size());
        for (const auto& entry : m_tiles)
        {
            TileKey key;
            key.x = int16_t(entry.second->TileX());
            key.y = int16_t(entry.second->TileY());
            out.push_back(key);
        }
    }

    size_t NavStore::ResidentCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tiles.size();
    }

    size_t NavStore::Footprint() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t total = 0;
        for (const auto& entry : m_tiles)
        {
            total += entry.second->Footprint();
        }
        for (const auto& entry : m_crossings)
        {
            total += entry.second.size() * sizeof(Crossing);
        }
        return total;
    }

    std::vector<Crossing> NavStore::CrossingsOf(const GateRef& from) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_crossings.find(PackGate(from));
        return it == m_crossings.end() ? std::vector<Crossing>() : it->second;
    }

    int NavStore::FindGateway(const NavTile& tile, uint8_t side, int position,
                              uint16_t region)
    {
        const std::vector<Gateway>& gateways = tile.Gateways();
        for (size_t i = 0; i < gateways.size(); ++i)
        {
            const Gateway& g = gateways[i];
            if (g.side == side && g.region == region &&
                position >= int(g.first) && position <= int(g.last))
            {
                return int(i);
            }
        }
        return -1;
    }

    /**
     * Match the shared border cell by cell. For each position along it, every surface
     * on this side is tested against every surface on the other, and a pair whose
     * heights are within the climb the BAKE used -- not one this file invents -- is a
     * step. The gateways those two surfaces belong to are then joined.
     *
     * Cell by cell rather than gateway by gateway on purpose: two gateways can face
     * each other along a border and still be separated by a wall running down the
     * middle of it, and only the per-cell test can tell.
     */
    void NavStore::StitchLocked(int tileX, int tileY, int neighbourX, int neighbourY)
    {
        const std::shared_ptr<const NavTile> nearTile = TileAtLocked(tileX, tileY);
        const std::shared_ptr<const NavTile> farTile =
            TileAtLocked(neighbourX, neighbourY);
        if (!nearTile || !farTile)
        {
            return;
        }

        const uint8_t side = NavTile::SideTowards(neighbourX - tileX, neighbourY - tileY);
        if (side >= NavTile::SIDE_COUNT)
        {
            return;
        }
        const uint8_t farSide = NavTile::FacingSide(side);

        // The stricter of the two bakes. They are normally identical; when they are not,
        // one of the tiles was baked by an older run, and the smaller climb is the one
        // that cannot invent a step neither bake believed in.
        const float climb = std::min(nearTile->Params().maxClimb, farTile->Params().maxClimb);

        // Accumulated per gateway pair, so a hundred matching cells produce one crossing
        // rather than a hundred. The width is the WIDEST match found: a crossing is
        // usable by anyone who fits through its most generous part.
        std::unordered_map<uint64_t, float> widths;

        std::vector<Surface> here;
        std::vector<Surface> there;

        for (int position = 0; position < CELLS_PER_TILE; ++position)
        {
            nearTile->SurfacesAt(NavTile::BorderCell(side, position), here);
            if (here.empty())
            {
                continue;
            }

            farTile->SurfacesAt(NavTile::BorderCell(farSide, position), there);
            if (there.empty())
            {
                continue;
            }

            for (const Surface& a : here)
            {
                for (const Surface& b : there)
                {
                    if (std::fabs(a.z - b.z) > climb)
                    {
                        continue;
                    }

                    const int gateA = FindGateway(*nearTile, side, position, a.region);
                    const int gateB = FindGateway(*farTile, farSide, position, b.region);
                    if (gateA < 0 || gateB < 0)
                    {
                        continue;
                    }

                    const float width = std::min(RestoreClearance(a.clearance),
                                                 RestoreClearance(b.clearance));

                    const uint64_t pair =
                        (uint64_t(uint32_t(gateA)) << 32) | uint64_t(uint32_t(gateB));

                    const auto it = widths.find(pair);
                    if (it == widths.end() || it->second < width)
                    {
                        widths[pair] = width;
                    }
                }
            }
        }

        for (const auto& entry : widths)
        {
            const uint16_t gateA = uint16_t(entry.first >> 32);
            const uint16_t gateB = uint16_t(entry.first & 0xFFFFFFFFu);

            GateRef from;
            from.tileX = int16_t(tileX);
            from.tileY = int16_t(tileY);
            from.gate = gateA;

            GateRef to;
            to.tileX = int16_t(neighbourX);
            to.tileY = int16_t(neighbourY);
            to.gate = gateB;

            Crossing out;
            out.to = to;
            out.width = entry.second;
            out.cost = CELL_SIZE;
            m_crossings[PackGate(from)].push_back(out);

            Crossing back;
            back.to = from;
            back.width = entry.second;
            back.cost = CELL_SIZE;
            m_crossings[PackGate(to)].push_back(back);
        }
    }

    void NavStore::UnstitchLocked(int tileX, int tileY)
    {
        const int16_t gx = int16_t(tileX);
        const int16_t gy = int16_t(tileY);

        for (auto it = m_crossings.begin(); it != m_crossings.end();)
        {
            const int16_t keyX = int16_t(uint16_t(it->first >> 32));
            const int16_t keyY = int16_t(uint16_t((it->first >> 16) & 0xFFFFu));

            if (keyX == gx && keyY == gy)
            {
                it = m_crossings.erase(it);
                continue;
            }

            // The tile is gone, so every crossing INTO it has to go too, even though
            // the gateway it leaves from is still resident. Leaving them is how a
            // search walks into a tile that unloaded under it.
            std::vector<Crossing>& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [gx, gy](const Crossing& c)
                                      {
                                          return c.to.tileX == gx && c.to.tileY == gy;
                                      }),
                       list.end());

            if (list.empty())
            {
                it = m_crossings.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool NavStore::SurfaceAt(float x, float y, float z, float tolerance, CellRef& cell,
                             Surface& surface) const
    {
        cell = CellAt(x, y);
        surface = Surface();

        const std::shared_ptr<const NavTile> tile = TileOf(cell);
        if (!tile)
        {
            return false;
        }

        surface = tile->SurfaceUnder(cell.InTile(), z, tolerance);
        return surface.Valid();
    }

    // --------------------------------------------------------------- NavStores ----

    NavStores& NavStores::Instance()
    {
        static NavStores instance;
        return instance;
    }

    NavStore& NavStores::For(uint32_t mapId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_maps.find(mapId);
        if (it == m_maps.end())
        {
            it = m_maps.emplace(mapId, std::unique_ptr<NavStore>(new NavStore(mapId)))
                     .first;
        }
        return *it->second;
    }

    const NavStore* NavStores::Find(uint32_t mapId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_maps.find(mapId);
        return it == m_maps.end() ? nullptr : it->second.get();
    }

    void NavStores::Drop(uint32_t mapId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_maps.erase(mapId);
    }

    void NavStores::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_maps.clear();
    }

    size_t NavStores::TileCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t total = 0;
        for (const auto& entry : m_maps)
        {
            total += entry.second->ResidentCount();
        }
        return total;
    }
}
