/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "nav/NavMeshIO.hpp"

#include <cstdio>
#include <string>

namespace Nav
{
    namespace
    {
        constexpr uint32_t MAGIC = 0x4853454D;   // "MESH" in file order

        /// Ceilings that a corrupt header cannot talk the reader past. Every one is
        /// loose enough never to refuse a real bake -- the worst tile of map 0 has nine
        /// thousand rectangles -- and tight enough that a garbage count cannot make the
        /// reader reserve a gigabyte before it finds out.
        constexpr uint32_t MAX_RECTS = 1u << 20;
        constexpr uint32_t MAX_PORTALS = 1u << 22;
        constexpr uint32_t MAX_AXIS = 1u << 22;
        constexpr uint32_t MAX_CRITICAL = 1u << 20;

        std::string g_meshDir;

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
        bool WVector(std::FILE* f, const std::vector<T>& v)
        {
            const uint32_t n = uint32_t(v.size());
            if (!WPod(f, n))
            {
                return false;
            }
            return n == 0 || std::fwrite(v.data(), sizeof(T), n, f) == n;
        }

        /// Read a vector whose length the FILE has to be able to back. The ceiling alone
        /// is not enough: it still lets a corrupt count reserve a vector the file could
        /// never fill, and the allocation happens before the read finds out.
        template <class T>
        bool RVector(std::FILE* f, std::vector<T>& v, uint32_t ceiling)
        {
            uint32_t n = 0;
            if (!RPod(f, n) || n > ceiling)
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

    void SetMeshDir(const std::string& dir) { g_meshDir = dir; }

    const std::string& MeshDir() { return g_meshDir; }

    std::string MeshFileName(uint32_t mapId, int tileX, int tileY)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "%04u_%02d_%02d.mesh", mapId, tileX, tileY);
        return std::string(name);
    }

    TileGeometry BuildTileGeometry(const NavTile& tile)
    {
        TileGeometry geometry;

        // One plan, three structures. Reading the tile's surfaces is the expensive part
        // of each of them, and doing it once is the whole reason these are built here
        // together rather than wherever each is first wanted.
        const TilePlan plan = ReadTilePlan(tile);

        std::vector<int32_t> cellToRect;
        geometry.mesh = BuildTileMesh(tile);

        const DistanceField field = BuildDistanceField(tile, plan);
        geometry.axis = BuildMedialAxis(tile, plan, field, 2.0f);
        geometry.reeb = BuildReebGraph(tile, plan, 1.0f);

        return geometry;
    }

    bool WriteTileGeometry(const std::string& path, uint32_t mapId, int tileX,
                           int tileY, const TileGeometry& geometry)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
        {
            return false;
        }

        bool ok = true;
        ok = ok && WPod(f, MAGIC);
        ok = ok && WPod(f, NAV_MESH_VERSION);
        ok = ok && WPod(f, mapId);
        ok = ok && WPod(f, int32_t(tileX));
        ok = ok && WPod(f, int32_t(tileY));

        ok = ok && WVector(f, geometry.mesh.rects);
        ok = ok && WVector(f, geometry.mesh.portals);
        ok = ok && WVector(f, geometry.mesh.first);
        ok = ok && WVector(f, geometry.axis);
        ok = ok && WVector(f, geometry.reeb.basins);
        ok = ok && WVector(f, geometry.reeb.passes);
        ok = ok && WVector(f, geometry.reeb.summits);
        ok = ok && WVector(f, geometry.reeb.arcs);

        if (std::fclose(f) != 0)
        {
            ok = false;
        }

        if (!ok)
        {
            // A half-written cache is worse than none: the reader would refuse it, but
            // only after the bake had reported success.
            std::remove(path.c_str());
        }

        return ok;
    }

    bool ReadTileGeometry(const std::string& path, uint32_t mapId, int tileX, int tileY,
                          TileGeometry& out)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            return false;
        }

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t fileMap = 0;
        int32_t fileX = 0;
        int32_t fileY = 0;

        bool ok = RPod(f, magic) && magic == MAGIC && RPod(f, version) &&
                  version == NAV_MESH_VERSION && RPod(f, fileMap) && fileMap == mapId &&
                  RPod(f, fileX) && fileX == tileX && RPod(f, fileY) && fileY == tileY;

        ok = ok && RVector(f, out.mesh.rects, MAX_RECTS);
        ok = ok && RVector(f, out.mesh.portals, MAX_PORTALS);
        ok = ok && RVector(f, out.mesh.first, MAX_RECTS + 1);
        ok = ok && RVector(f, out.axis, MAX_AXIS);
        ok = ok && RVector(f, out.reeb.basins, MAX_CRITICAL);
        ok = ok && RVector(f, out.reeb.passes, MAX_CRITICAL);
        ok = ok && RVector(f, out.reeb.summits, MAX_CRITICAL);
        ok = ok && RVector(f, out.reeb.arcs, MAX_CRITICAL);

        std::fclose(f);

        // The index table has one entry per rectangle plus the terminator, and every
        // portal must name a rectangle that exists. Checked because these are what the
        // search indexes with: a file that passed the length checks and still carried a
        // neighbour of four billion would be read straight into an out-of-bounds walk.
        if (ok)
        {
            ok = out.mesh.first.size() == out.mesh.rects.size() + 1;
        }

        if (ok)
        {
            for (const Portal& portal : out.mesh.portals)
            {
                if (portal.rect >= out.mesh.rects.size() ||
                    portal.neighbour >= out.mesh.rects.size())
                {
                    ok = false;
                    break;
                }
            }
        }

        if (ok)
        {
            for (uint32_t at : out.mesh.first)
            {
                if (at > out.mesh.portals.size())
                {
                    ok = false;
                    break;
                }
            }
        }

        if (!ok)
        {
            out = TileGeometry();
        }

        return ok;
    }
}
