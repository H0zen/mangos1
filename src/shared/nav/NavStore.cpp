#include "nav/NavStore.hpp"

#include "nav/NavMeshIO.hpp"

#include "nav/NavTileIO.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>   // std::abs over ints: libstdc++ leaks it through <cmath>, libc++ does not
#include <mutex>

namespace
{
    /// Wanted tiles remembered at once. A search that misses more than this is
    /// asking for ground nobody is anywhere near, and forgetting the tail is the
    /// right answer -- it will be wanted again if it is wanted at all.
    constexpr size_t MAX_WANTED = 32;
}

namespace Nav
{
    // ---------------------------------------------------------------- NavStore ----

    bool NavStore::LoadTile(int tileX, int tileY)
    {
        return LoadTileInternal(tileX, tileY, true);
    }

    bool NavStore::LoadTileInternal(int tileX, int tileY, bool pinned)
    {
        const uint32_t key = Pack(tileX, tileY);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_tiles.find(key);
            if (it != m_tiles.end())
            {
                // Already here -- but PROMOTE it if a grid is what is asking now. The
                // pump loads tiles unpinned; a player then walks onto that grid and
                // this returned early without claiming it, so the cap could evict a
                // tile the grid was still holding. Routing on the player's own ground
                // then answered "no ground here".
                it->second.pinned = it->second.pinned || pinned;
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
        {
            const auto it = m_tiles.find(key);
            if (it != m_tiles.end())
            {
                it->second.pinned = it->second.pinned || pinned;
                return true;
            }
        }

        Resident resident;
        resident.tile = std::move(tile);
        resident.pinned = pinned;
        resident.touched = ++m_clock;
        m_tiles.emplace(key, std::move(resident));

        m_wanted.erase(std::remove(m_wanted.begin(), m_wanted.end(), key),
                       m_wanted.end());

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
        UnstitchMeshLocked(tileX, tileY);
    }

    void NavStore::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tiles.clear();
        m_crossings.clear();
        m_meshCrossings.clear();

        // The want list too. It is a list of tiles some search asked for BEFORE this
        // reset, and leaving it behind meant PumpWanted spent the next few ticks reading
        // files back in for routes that no longer exist -- a clear that does not clear.
        m_wanted.clear();
    }

    bool NavStore::IsResident(int tileX, int tileY) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tiles.find(Pack(tileX, tileY)) != m_tiles.end();
    }

    std::shared_ptr<const NavTile> NavStore::TileAtLocked(int tileX, int tileY) const
    {
        const auto it = m_tiles.find(Pack(tileX, tileY));
        if (it == m_tiles.end())
        {
            return nullptr;
        }

        it->second.touched = ++m_clock;
        return it->second.tile;
    }

    std::shared_ptr<const NavTile> NavStore::TileAt(int tileX, int tileY) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return TileAtLocked(tileX, tileY);
    }

    std::shared_ptr<const TileMesh> NavStore::MeshOf(int tileX, int tileY) const
    {
        std::shared_ptr<const NavTile> tile;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_tiles.find(Pack(tileX, tileY));
            if (it == m_tiles.end())
            {
                return nullptr;
            }

            it->second.touched = ++m_clock;
            if (it->second.mesh)
            {
                return it->second.mesh;
            }

            tile = it->second.tile;
        }

        if (!tile)
        {
            return nullptr;
        }

        // The baked cache first. It is a cache and nothing more: a missing or stale file
        // is not an error and refuses nothing, it only means this tile pays for its own
        // derivation. That is what let the mesh ship before everything that reads cells
        // had been moved onto it.
        std::shared_ptr<const TileMesh> built;

        if (!MeshDir().empty())
        {
            TileGeometry geometry;
            const std::string path =
                MeshDir() + "/" + MeshFileName(m_mapId, tileX, tileY);

            if (ReadTileGeometry(path, m_mapId, tileX, tileY, geometry))
            {
                built = std::make_shared<const TileMesh>(std::move(geometry.mesh));
            }
        }

        // Outside the lock. This walks a quarter of a million cells, and every other
        // search on the map would be waiting on it.
        if (!built)
        {
            built = std::make_shared<const TileMesh>(BuildTileMesh(*tile));
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_tiles.find(Pack(tileX, tileY));
            if (it == m_tiles.end() || it->second.tile != tile)
            {
                // Unloaded, or replaced by a re-load, while we were building. The mesh
                // describes a tile that is no longer the one under this key, so it is
                // thrown away rather than published against the wrong file.
                return nullptr;
            }

            if (!it->second.mesh)
            {
                it->second.mesh = built;

                // A mesh appearing is when this tile becomes joinable to its neighbours:
                // the rims are matched from two meshes, so neither the tile's arrival
                // nor the neighbour's could do it alone. Whichever derives second is the
                // one that matches the pair, and the match is idempotent, so a tile that
                // is re-derived does not double its own crossings.
                StitchMeshLocked(tileX, tileY);
            }

            return it->second.mesh;
        }
    }

    std::vector<MeshCrossing> NavStore::MeshCrossingsOf(int tileX, int tileY,
                                                        uint32_t rect) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const auto it = m_meshCrossings.find(MeshKey(tileX, tileY, rect));
        return it == m_meshCrossings.end() ? std::vector<MeshCrossing>() : it->second;
    }

    void NavStore::StitchMeshLocked(int tileX, int tileY) const
    {
        // Meshes are derived outside the lock, and this runs under it. So the stitch is
        // only made between tiles whose mesh is ALREADY built: a tile that has not been
        // routed through yet has none, and there is nothing to lose by waiting -- the
        // first route into it derives the mesh and this runs again from there.
        const auto meshFor = [this](int tx, int ty) -> std::shared_ptr<const TileMesh>
        {
            const auto it = m_tiles.find(Pack(tx, ty));
            return it == m_tiles.end() ? nullptr : it->second.mesh;
        };

        const std::shared_ptr<const NavTile> nearTile = TileAtLocked(tileX, tileY);
        const std::shared_ptr<const TileMesh> nearMesh = meshFor(tileX, tileY);
        if (!nearTile || !nearMesh)
        {
            return;
        }

        const int dx[4] = {-1, 1, 0, 0};
        const int dy[4] = {0, 0, -1, 1};

        for (int side = 0; side < 4; ++side)
        {
            const int otherX = tileX + dx[side];
            const int otherY = tileY + dy[side];

            const std::shared_ptr<const NavTile> farTile =
                TileAtLocked(otherX, otherY);
            const std::shared_ptr<const TileMesh> farMesh = meshFor(otherX, otherY);
            if (!farTile || !farMesh)
            {
                continue;
            }

            // Idempotent: whatever this pair had is dropped before it is rebuilt. Both
            // tiles run this when their own mesh appears, and without the clear the
            // second run would double every crossing between them.
            const auto forget = [this](int fromX, int fromY, int toX, int toY)
            {
                for (auto it = m_meshCrossings.begin(); it != m_meshCrossings.end();)
                {
                    if (static_cast<uint32_t>(it->first >> 32) != Pack(fromX, fromY))
                    {
                        ++it;
                        continue;
                    }

                    std::vector<MeshCrossing>& list = it->second;
                    list.erase(std::remove_if(list.begin(), list.end(),
                                              [toX, toY](const MeshCrossing& crossing)
                                              {
                                                  return crossing.farTileX == toX &&
                                                         crossing.farTileY == toY;
                                              }),
                               list.end());

                    it = list.empty() ? m_meshCrossings.erase(it) : std::next(it);
                }
            };

            forget(tileX, tileY, otherX, otherY);
            forget(otherX, otherY, tileX, tileY);

            std::vector<MeshCrossing> matched;
            MatchRims(*nearTile, *nearMesh, *farTile, *farMesh, matched);
            MatchRims(*farTile, *farMesh, *nearTile, *nearMesh, matched);

            for (const MeshCrossing& crossing : matched)
            {
                m_meshCrossings[MeshKey(crossing.nearTileX, crossing.nearTileY,
                                        crossing.nearRect)]
                    .push_back(crossing);
            }
        }
    }

    void NavStore::UnstitchMeshLocked(int tileX, int tileY) const
    {
        const uint32_t leaving = Pack(tileX, tileY);

        for (auto it = m_meshCrossings.begin(); it != m_meshCrossings.end();)
        {
            // Crossings STARTING here go with the tile. Ones ending here have to be
            // pulled out of a neighbour's list, which is why the value is filtered
            // rather than the key alone being erased.
            if (static_cast<uint32_t>(it->first >> 32) == leaving)
            {
                it = m_meshCrossings.erase(it);
                continue;
            }

            std::vector<MeshCrossing>& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [tileX, tileY](const MeshCrossing& crossing)
                                      {
                                          return crossing.farTileX == tileX &&
                                                 crossing.farTileY == tileY;
                                      }),
                       list.end());

            if (list.empty())
            {
                it = m_meshCrossings.erase(it);
            }
            else
            {
                ++it;
            }
        }
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
            key.x = int16_t(entry.second.tile->TileX());
            key.y = int16_t(entry.second.tile->TileY());
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
            total += entry.second.tile->Footprint();
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

    void NavStore::Want(int tileX, int tileY) const
    {
        if (tileX < 0 || tileY < 0 || tileX >= TILES_PER_MAP ||
            tileY >= TILES_PER_MAP)
        {
            return;
        }

        const uint32_t key = Pack(tileX, tileY);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tiles.find(key) != m_tiles.end())
        {
            return;   // already here; nothing was missing
        }

        if (std::find(m_wanted.begin(), m_wanted.end(), key) != m_wanted.end())
        {
            return;
        }

        if (m_wanted.size() >= MAX_WANTED)
        {
            return;
        }

        m_wanted.push_back(key);
    }

    void NavStore::WantAlong(float fromX, float fromY, float toX, float toY) const
    {
        // Every tile the straight line passes through, plus the two ends. Not the
        // route -- there is no route, that is why this is being called -- but the
        // corridor a route would most likely need, which is what the next attempt
        // will search once these are in.
        const CellRef a = CellAt(fromX, fromY);
        const CellRef b = CellAt(toX, toY);
        if (!a.Valid() || !b.Valid())
        {
            return;
        }

        const int ax = a.TileX();
        const int ay = a.TileY();
        const int bx = b.TileX();
        const int by = b.TileY();

        const int steps = std::max(std::abs(bx - ax), std::abs(by - ay));
        if (steps == 0)
        {
            Want(ax, ay);
            return;
        }

        int prevX = ax;
        int prevY = ay;
        Want(ax, ay);

        for (int i = 1; i <= steps; ++i)
        {
            const float t = float(i) / float(steps);
            const int nx = ax + int(std::lround(t * float(bx - ax)));
            const int ny = ay + int(std::lround(t * float(by - ay)));

            // A diagonal hop in tile space has no rim. The two orthogonal tiles
            // are the stepping stones coarse actually walks, and omitting them
            // is how a chase across a corner stayed Wall forever.
            if (nx != prevX && ny != prevY)
            {
                Want(prevX, ny);
                Want(nx, prevY);
            }

            Want(nx, ny);
            prevX = nx;
            prevY = ny;
        }
    }

    size_t NavStore::PumpWanted(size_t maxLoads)
    {
        std::vector<uint32_t> take;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (!m_wanted.empty() && take.size() < maxLoads)
            {
                take.push_back(m_wanted.back());
                m_wanted.pop_back();
            }
        }

        size_t loaded = 0;
        for (uint32_t key : take)
        {
            const int tileX = int(int16_t(uint16_t(key >> 16)));
            const int tileY = int(int16_t(uint16_t(key & 0xFFFFu)));

            // Unpinned: no grid is holding this one, so the cap below may drop it
            // again once nothing has used it for a while.
            if (LoadTileInternal(tileX, tileY, false))
            {
                ++loaded;
            }
        }

        EvictUnpinned();
        return loaded;
    }

    void NavStore::EvictUnpinned()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t unpinned = 0;
        for (const auto& entry : m_tiles)
        {
            if (!entry.second.pinned)
            {
                ++unpinned;
            }
        }

        while (unpinned > MAX_UNPINNED)
        {
            // The least recently touched of the ones no grid holds. Linear, over a few
            // dozen entries, once per pump -- cheaper than keeping an order.
            auto oldest = m_tiles.end();
            for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it)
            {
                if (!it->second.pinned &&
                    (oldest == m_tiles.end() ||
                     it->second.touched < oldest->second.touched))
                {
                    oldest = it;
                }
            }

            if (oldest == m_tiles.end())
            {
                break;
            }

            const int tileX = int(int16_t(uint16_t(oldest->first >> 16)));
            const int tileY = int(int16_t(uint16_t(oldest->first & 0xFFFFu)));

            m_tiles.erase(oldest);
            UnstitchLocked(tileX, tileY);
            UnstitchMeshLocked(tileX, tileY);
            --unpinned;
        }
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
        if (side >= NavTile::SIDE_BORDER_COUNT)
        {
            return;
        }
        const uint8_t farSide = NavTile::FacingSide(side);

        // The stricter of the two bakes. They are normally identical; when they are not,
        // one of the tiles was baked by an older run, and the smaller limit is the one
        // that cannot invent a step neither bake believed in.
        //
        // Through ClimbWindow, and not the bare climb: the bake links a hillside cell to
        // its neighbour over the rise the slope limit permits, so a border judged on the
        // step alone would cut every slope steeper than a kerb exactly at the tile edge
        // -- a seam no map has and no bake believes in.
        const float climb = ClimbWindow(
            std::min(nearTile->Params().maxClimb, farTile->Params().maxClimb),
            std::min(nearTile->Params().maxSlopeDeg, farTile->Params().maxSlopeDeg),
            CELL_SIZE);

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

        // THE MESH FIRST. It carries height, region, area and clearance -- everything a
        // profile judges a surface by -- so there is nothing the cell version answers
        // that this does not, and it answers from a structure a hundred times smaller.
        //
        // The cells stay as the fallback for exactly one window: a tile that is resident
        // but whose mesh has not been derived yet. A position query arriving in it still
        // has to be answered, and deriving a mesh here would put a pass over a quarter of
        // a million cells inside a call the world makes constantly.
        const std::shared_ptr<const TileMesh> mesh =
            MeshOf(cell.TileX(), cell.TileY());

        if (mesh && MeshSurfaceAt(*tile, *mesh, x, y, z, tolerance, surface))
        {
            return true;
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
