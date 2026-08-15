#include "nav/NavTileIO.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Nav
{
    namespace
    {
        constexpr uint32_t MAGIC = 0x3156414E;   // "NAV1" in file order

        /// A run of the area plane. Runs are capped so the length fits a uint16, which
        /// costs one extra run per 65535 identical cells -- at most four per tile.
        constexpr uint32_t MAX_RUN = 0xFFFF;

        /// Gateways one tile may have. A gateway is a whole RUN of border cells, so a
        /// real tile has tens; this is loose enough never to refuse a legitimate bake
        /// and tight enough that the squared cost matrix stays a few megabytes.
        constexpr uint32_t MAX_GATEWAYS = 1024;

        std::string g_navDir;

        template <class T>
        bool WPod(std::FILE* f, const T& v)
        {
            return std::fwrite(&v, sizeof(T), 1, f) == 1;
        }

        template <class T>
        bool RPod(std::FILE* f, T& v)
        {
            return std::fread(&v, sizeof(T), 1, f) == 1;
        }

        /// Bytes left in the file from here. A count is only believable if the file
        /// still holds that many elements; a fixed ceiling is not enough, because it
        /// still lets a corrupt header reserve a vector the file could never fill.
        long RemainingBytes(std::FILE* f)
        {
            const long here = std::ftell(f);
            if (here < 0 || std::fseek(f, 0, SEEK_END) != 0)
            {
                return -1;
            }
            const long end = std::ftell(f);
            if (end < 0 || std::fseek(f, here, SEEK_SET) != 0)
            {
                return -1;
            }
            return end - here;
        }

        template <class T>
        bool WPlane(std::FILE* f, const std::vector<T>& v)
        {
            const uint32_t n = uint32_t(v.size());
            if (!WPod(f, n))
            {
                return false;
            }
            return n == 0 || std::fwrite(v.data(), sizeof(T), n, f) == n;
        }

        template <class T>
        bool RPlane(std::FILE* f, std::vector<T>& v, uint32_t expected)
        {
            uint32_t n = 0;
            if (!RPod(f, n) || n != expected)
            {
                return false;
            }

            const long remaining = RemainingBytes(f);
            if (remaining < 0 || uint64_t(remaining) < uint64_t(n) * sizeof(T))
            {
                return false;
            }

            v.resize(n);
            return n == 0 || std::fread(v.data(), sizeof(T), n, f) == n;
        }
    }

    void SetNavDir(const std::string& dir) { g_navDir = dir; }

    const std::string& NavDir() { return g_navDir; }

    std::string NavTileFileName(uint32_t mapId, int tileX, int tileY)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "%04u_%02d_%02d.nav", mapId, tileX, tileY);
        return std::string(name);
    }

    bool HasNavTile(uint32_t mapId, int tileX, int tileY)
    {
        const std::string path = g_navDir + "/" + NavTileFileName(mapId, tileX, tileY);
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            return false;
        }
        std::fclose(f);
        return true;
    }

    bool WriteNavTile(const std::string& path, uint32_t mapId, const NavTile& tile)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
        {
            return false;
        }

        bool ok = true;

        // -- header. The grid parameters are written so the reader can refuse a tile
        //    baked against a different grid, which parses perfectly and is wrong.
        ok = ok && WPod(f, MAGIC);
        ok = ok && WPod(f, NAV_TILE_VERSION);
        ok = ok && WPod(f, mapId);
        ok = ok && WPod(f, int32_t(tile.TileX()));
        ok = ok && WPod(f, int32_t(tile.TileY()));
        ok = ok && WPod(f, tile.BaseZ());
        ok = ok && WPod(f, uint32_t(CELLS_PER_TILE));
        ok = ok && WPod(f, float(CELL_SIZE));
        ok = ok && WPod(f, float(Z_QUANTUM));
        ok = ok && WPod(f, tile.Params().agentHeight);
        ok = ok && WPod(f, tile.Params().maxClimb);
        ok = ok && WPod(f, tile.Params().maxSlopeDeg);
        ok = ok && WPod(f, tile.Params().swimDepth);

        // -- the area plane, run-length encoded. Most of a tile is a handful of long
        //    runs: solid ground outdoors, solid nothing over a lake or off the map.
        std::vector<uint8_t> runValue;
        std::vector<uint16_t> runLength;
        uint32_t walkable = 0;

        const std::vector<uint8_t>& areas = tile.AreaPlane();
        for (size_t i = 0; i < areas.size();)
        {
            const uint8_t value = areas[i];
            size_t run = 1;
            while (i + run < areas.size() && areas[i + run] == value && run < MAX_RUN)
            {
                ++run;
            }

            runValue.push_back(value);
            runLength.push_back(uint16_t(run));

            if (Walkable(value))
            {
                walkable += uint32_t(run);
            }

            i += run;
        }

        ok = ok && WPod(f, walkable);
        ok = ok && WPlane(f, runValue);
        ok = ok && WPlane(f, runLength);

        // -- the payload planes, walkable cells only and in cell order. The area plane
        //    above says which cells those are, so no index needs storing.
        std::vector<uint16_t> z;
        std::vector<uint8_t> clearance;
        std::vector<uint16_t> region;
        z.reserve(walkable);
        clearance.reserve(walkable);
        region.reserve(walkable);

        for (size_t i = 0; i < areas.size(); ++i)
        {
            if (Walkable(areas[i]))
            {
                z.push_back(tile.ZPlane()[i]);
                clearance.push_back(tile.ClearancePlane()[i]);
                region.push_back(tile.RegionPlane()[i]);
            }
        }

        ok = ok && WPlane(f, z);
        ok = ok && WPlane(f, clearance);
        ok = ok && WPlane(f, region);

        // -- stacked surfaces, field by field. Small enough that the writes cost
        //    nothing, and it removes the struct's padding from the file's contract.
        const std::vector<StackedLayer>& stacked = tile.Stacked();
        ok = ok && WPod(f, uint32_t(stacked.size()));
        for (const StackedLayer& l : stacked)
        {
            ok = ok && WPod(f, l.cell);
            ok = ok && WPod(f, l.z);
            ok = ok && WPod(f, l.region);
            ok = ok && WPod(f, l.area);
            ok = ok && WPod(f, l.clearance);
        }

        const std::vector<Region>& regions = tile.Regions();
        ok = ok && WPod(f, uint32_t(regions.size()));
        for (const Region& r : regions)
        {
            ok = ok && WPod(f, r.cellCount);
            ok = ok && WPod(f, r.minZ);
            ok = ok && WPod(f, r.maxZ);
            ok = ok && WPod(f, r.areas);
            ok = ok && WPod(f, r.maxClearance);
        }

        const std::vector<Gateway>& gateways = tile.Gateways();
        ok = ok && WPod(f, uint32_t(gateways.size()));
        for (const Gateway& g : gateways)
        {
            ok = ok && WPod(f, g.side);
            ok = ok && WPod(f, g.first);
            ok = ok && WPod(f, g.last);
            ok = ok && WPod(f, g.region);
            ok = ok && WPod(f, g.firstZ);
            ok = ok && WPod(f, g.lastZ);
            ok = ok && WPod(f, g.width);
            ok = ok && WPod(f, g.x);
            ok = ok && WPod(f, g.y);
            ok = ok && WPod(f, g.z);
            ok = ok && WPod(f, g.cell);
            ok = ok && WPod(f, g.layer);
        }

        ok = ok && WPlane(f, tile.GatewayCostMatrix());

        const std::vector<Link>& links = tile.Links();
        ok = ok && WPod(f, uint32_t(links.size()));
        for (const Link& link : links)
        {
            ok = ok && WPod(f, link.fromGate);
            ok = ok && WPod(f, link.toGate);
            ok = ok && WPod(f, link.cost);
            ok = ok && WPod(f, uint8_t(link.bidirectional ? 1 : 0));
        }

        if (std::fclose(f) != 0)
        {
            ok = false;
        }

        if (!ok)
        {
            std::remove(path.c_str());
        }

        return ok;
    }

    bool ReadNavTile(const std::string& path, uint32_t mapId, NavTile& out)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            return false;
        }

        struct Closer
        {
            std::FILE* f;
            ~Closer() { std::fclose(f); }
        } closer{f};

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t fileMap = 0;
        int32_t tileX = 0;
        int32_t tileY = 0;
        float baseZ = 0.0f;
        uint32_t cells = 0;
        float cellSize = 0.0f;
        float zQuantum = 0.0f;

        if (!RPod(f, magic) || magic != MAGIC ||
            !RPod(f, version) || version != NAV_TILE_VERSION ||
            !RPod(f, fileMap) || fileMap != mapId ||
            !RPod(f, tileX) || !RPod(f, tileY) || !RPod(f, baseZ) ||
            !RPod(f, cells) || cells != uint32_t(CELLS_PER_TILE) ||
            !RPod(f, cellSize) || cellSize != float(CELL_SIZE) ||
            !RPod(f, zQuantum) || zQuantum != float(Z_QUANTUM))
        {
            return false;
        }

        TileParams params;
        if (!RPod(f, params.agentHeight) || !RPod(f, params.maxClimb) ||
            !RPod(f, params.maxSlopeDeg) || !RPod(f, params.swimDepth))
        {
            return false;
        }

        NavTile tile;
        tile.Reset(tileX, tileY, baseZ);
        tile.SetParams(params);

        uint32_t walkable = 0;
        if (!RPod(f, walkable) || walkable > uint32_t(CELLS_PER_TILE_SQ))
        {
            return false;
        }

        std::vector<uint8_t> runValue;
        std::vector<uint16_t> runLength;
        {
            uint32_t runs = 0;
            const long remaining = RemainingBytes(f);
            if (!RPod(f, runs) || remaining < 0 ||
                uint64_t(runs) > uint64_t(CELLS_PER_TILE_SQ))
            {
                return false;
            }

            const long left = RemainingBytes(f);
            if (left < 0 || uint64_t(left) < uint64_t(runs))
            {
                return false;
            }

            runValue.resize(runs);
            if (runs && std::fread(runValue.data(), 1, runs, f) != runs)
            {
                return false;
            }

            if (!RPlane(f, runLength, runs))
            {
                return false;
            }
        }

        // Expand the runs, counting the walkable cells as we go. A run table that does
        // not cover the tile exactly, or that disagrees with the walkable count the
        // payload planes were sized from, is a corrupt file and not a short one.
        std::vector<uint8_t>& areas = tile.MutableAreaPlane();
        size_t at = 0;
        uint32_t seen = 0;
        for (size_t r = 0; r < runValue.size(); ++r)
        {
            const size_t run = size_t(runLength[r]);
            if (run == 0 || at + run > areas.size())
            {
                return false;
            }

            std::memset(areas.data() + at, runValue[r], run);
            if (Walkable(runValue[r]))
            {
                seen += uint32_t(run);
            }
            at += run;
        }

        if (at != areas.size() || seen != walkable)
        {
            return false;
        }

        std::vector<uint16_t> z;
        std::vector<uint8_t> clearance;
        std::vector<uint16_t> region;
        if (!RPlane(f, z, walkable) || !RPlane(f, clearance, walkable) ||
            !RPlane(f, region, walkable))
        {
            return false;
        }

        {
            size_t at2 = 0;
            for (size_t i = 0; i < areas.size(); ++i)
            {
                if (Walkable(areas[i]))
                {
                    tile.MutableZPlane()[i] = z[at2];
                    tile.MutableClearancePlane()[i] = clearance[at2];
                    tile.MutableRegionPlane()[i] = region[at2];
                    ++at2;
                }
            }
        }

        uint32_t stackedCount = 0;
        if (!RPod(f, stackedCount))
        {
            return false;
        }
        {
            const long left = RemainingBytes(f);
            if (left < 0 || uint64_t(left) < uint64_t(stackedCount) * 10u)
            {
                return false;
            }
        }
        for (uint32_t i = 0; i < stackedCount; ++i)
        {
            StackedLayer l;
            if (!RPod(f, l.cell) || !RPod(f, l.z) || !RPod(f, l.region) ||
                !RPod(f, l.area) || !RPod(f, l.clearance))
            {
                return false;
            }
            if (l.cell >= uint32_t(CELLS_PER_TILE_SQ))
            {
                return false;
            }
            tile.AddStacked(l);
        }
        tile.SortStacked();

        uint32_t regionCount = 0;
        if (!RPod(f, regionCount))
        {
            return false;
        }
        if (regionCount > 0xFFFFu)
        {
            return false;
        }
        {
            const long left = RemainingBytes(f);
            if (left < 0 || uint64_t(left) < uint64_t(regionCount) * 18u)
            {
                return false;
            }
        }
        std::vector<Region>& regions = tile.MutableRegions();
        regions.resize(regionCount);
        for (uint32_t i = 0; i < regionCount; ++i)
        {
            Region& r = regions[i];
            if (!RPod(f, r.cellCount) || !RPod(f, r.minZ) || !RPod(f, r.maxZ) ||
                !RPod(f, r.areas) || !RPod(f, r.maxClearance))
            {
                return false;
            }
        }

        // Capped well below what the field could hold, because the cost matrix is
        // gatewayCount SQUARED: at the uint32 ceiling that product overflows before it
        // is ever compared against the file's size, and the check meant to catch a
        // corrupt count would pass on a small number.
        uint32_t gatewayCount = 0;
        if (!RPod(f, gatewayCount) || gatewayCount > MAX_GATEWAYS)
        {
            return false;
        }
        {
            const long left = RemainingBytes(f);
            // 37 bytes a gateway, counted field by field. It grew by six in version 2
            // -- the cell and the layer -- and a guard left at the old width still
            // passes on a file six bytes short per gateway, which is the truncation it
            // exists to catch.
            if (left < 0 || uint64_t(left) < uint64_t(gatewayCount) * 37u)
            {
                return false;
            }
        }
        std::vector<Gateway>& gateways = tile.MutableGateways();
        gateways.resize(gatewayCount);
        for (uint32_t i = 0; i < gatewayCount; ++i)
        {
            Gateway& g = gateways[i];
            if (!RPod(f, g.side) || !RPod(f, g.first) || !RPod(f, g.last) ||
                !RPod(f, g.region) || !RPod(f, g.firstZ) || !RPod(f, g.lastZ) ||
                !RPod(f, g.width) || !RPod(f, g.x) || !RPod(f, g.y) ||
                !RPod(f, g.z) || !RPod(f, g.cell) || !RPod(f, g.layer))
            {
                return false;
            }

            if (g.side >= NavTile::SIDE_COUNT || g.region >= regions.size() ||
                g.cell >= uint32_t(CELLS_PER_TILE_SQ))
            {
                return false;
            }

            // The run bounds mean nothing for a link mouth, which is one cell.
            if (g.side < NavTile::SIDE_BORDER_COUNT &&
                (g.first > g.last || g.last >= uint16_t(CELLS_PER_TILE)))
            {
                return false;
            }
        }

        // The matrix is indexed as [from * n + to] with no bounds test on the product,
        // so its size has to be exactly n^2 and not merely large enough.
        if (!RPlane(f, tile.MutableGatewayCost(), gatewayCount * gatewayCount))
        {
            return false;
        }

        uint32_t linkCount = 0;
        if (!RPod(f, linkCount) || linkCount > MAX_GATEWAYS)
        {
            return false;
        }
        {
            const long left = RemainingBytes(f);
            if (left < 0 || uint64_t(left) < uint64_t(linkCount) * 9u)
            {
                return false;
            }
        }
        std::vector<Link>& links = tile.MutableLinks();
        links.resize(linkCount);
        for (uint32_t i = 0; i < linkCount; ++i)
        {
            Link& link = links[i];
            uint8_t both = 0;
            if (!RPod(f, link.fromGate) || !RPod(f, link.toGate) ||
                !RPod(f, link.cost) || !RPod(f, both))
            {
                return false;
            }
            link.bidirectional = (both != 0);

            // Both mouths must exist. The search indexes the gateway table with these
            // without re-checking, and a link naming a gateway that is not there is the
            // one corruption that reads as a valid tile and then walks off the end.
            if (link.fromGate >= gatewayCount || link.toGate >= gatewayCount ||
                link.fromGate == link.toGate)
            {
                return false;
            }

            // The cost is added to a running g in the coarse search and compared with
            // every other edge. A negative one makes the queue prefer going round the
            // link forever, and a NaN compares false against everything, so the search
            // would neither settle nor terminate on a route that touched this tile.
            if (!(link.cost >= 0.0f) || !std::isfinite(link.cost))
            {
                return false;
            }
        }

        // Every walkable cell names a region, and the search indexes the region table
        // with it without re-checking. A cell naming a region that does not exist is
        // the one corruption that would read as a valid tile and then index past the
        // end of the table on the first query that touched it.
        for (size_t i = 0; i < areas.size(); ++i)
        {
            if (Walkable(areas[i]) && tile.RegionPlane()[i] >= regions.size())
            {
                return false;
            }
        }

        for (const StackedLayer& layer : tile.Stacked())
        {
            if (layer.region >= regions.size())
            {
                return false;
            }
        }

        out = std::move(tile);
        return true;
    }
}
