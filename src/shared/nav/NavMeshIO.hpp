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

#pragma once

/**
 * @file NavMeshIO.hpp
 * @brief THE MESH AS A FILE -- baked once, not derived on every server start.
 *
 * `BuildTileMesh` walks a quarter of a million cells, `BuildDistanceField` walks them
 * twice more and `BuildReebGraph` sorts them. That is a fraction of a second per tile,
 * which is nothing in a baker and is a stall in a map's tick: the store builds the mesh
 * the first time a route wants a tile, and that first route pays for the whole tile.
 *
 * These are baked instead, into a file beside the tile's own. A separate file rather
 * than a section inside `.nav`, deliberately:
 *
 * - The `.nav` format carries a version, and a tile baked by an older extractor is
 *   refused outright -- silence over that square of the world. Adding a section would
 *   force a version bump and a full re-bake of every map for a structure the runtime can
 *   still derive if it is missing.
 * - This one is a CACHE. A missing `.mesh` is not an error and never refuses anything:
 *   the store falls back to deriving it, exactly as it does today. That is what lets the
 *   mesh ship before everything that reads cells has been moved onto it.
 * - When the cell grid finally goes, this becomes the tile and the fallback goes with
 *   it. Keeping it separate now is what makes that a deletion rather than a migration.
 *
 * The file carries the mesh, the medial axis and the critical points together, because
 * all three are derived from the same plan and re-reading a tile to build one of them is
 * the cost being avoided.
 */

#include "nav/MedialAxis.hpp"
#include "nav/NavMesh.hpp"
#include "nav/ReebGraph.hpp"

#include <cstdint>
#include <string>

namespace Nav
{
    /// Bumped whenever any of the three structures changes shape. A file at the wrong
    /// version is ignored and rebuilt, never read half-understood.
    constexpr uint32_t NAV_MESH_VERSION = 1;

    /// Everything one tile's geometry is worth precomputing.
    struct TileGeometry
    {
        TileMesh mesh;
        std::vector<AxisVertex> axis;
        ReebGraph reeb;
    };

    /// `0000_31_57.mesh`, beside the `.nav` of the same tile.
    std::string MeshFileName(uint32_t mapId, int tileX, int tileY);

    /**
     * @brief Derive everything for one tile, in one pass over its plan.
     *
     * The three structures share a `TilePlan`, which is the expensive part of each of
     * them; building them together costs one read of the tile rather than three.
     */
    TileGeometry BuildTileGeometry(const NavTile& tile);

    bool WriteTileGeometry(const std::string& path, uint32_t mapId, int tileX,
                           int tileY, const TileGeometry& geometry);

    /**
     * @brief Read one back.
     *
     * @return False for missing, stale, truncated or another tile's file -- all of which
     *         mean the same thing to the caller: derive it instead.
     */
    bool ReadTileGeometry(const std::string& path, uint32_t mapId, int tileX, int tileY,
                          TileGeometry& out);

    /// Where the baker writes and the store looks. Set once at start-up, like the tile
    /// and nav directories it sits beside.
    void SetMeshDir(const std::string& dir);
    const std::string& MeshDir();
}
