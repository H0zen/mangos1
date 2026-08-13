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

#ifndef MANGOS_H_MOVE_MAP_SHARED_DEFINES
#define MANGOS_H_MOVE_MAP_SHARED_DEFINES

#include "DetourNavMesh.h"
#include "Platform/Define.h"

#define MMAP_MAGIC 0x4d4d4150   // 'MMAP'
// Version 6 adds orthogonal terrain/liquid border cells during fused navmesh baking.
// Version 7 stores the TERRAIN BIT in each polygon's flags instead of a bare 1. Detour
// filters on flags, never on the area id, so a version 6 tile tells every query that
// every polygon is ground: a swimmer's WATER|MAGMA|SLIME mask matches nothing and a
// walker is cleared to cross magma. The two are indistinguishable at load time -- both
// are "a poly with flags set" -- so the version is what makes a stale bake say so
// instead of pathing wrongly for as long as it stays on disk.
// Version 8 records the WIDTH OF dtPolyRef, and drops the bool bitfield.
//
// DT_POLYREF64 widens dtPolyRef to 64 bits, and dtLink -- the per-link record that
// dtCreateNavMeshData sizes the tile blob from -- CONTAINS a dtPolyRef. So the whole
// on-disk layout moves with that width, exactly as Detour's own header warns ("tiles
// build using 32bit refs are not compatible with 64bit refs"). DT_NAVMESH_VERSION does
// NOT change when DT_POLYREF64 is defined, so nothing here caught it: a tile baked by a
// 32-bit-ref binary passed magic, dtVersion and mmapVersion, and was then read with
// every offset past the header shifted. The width is now part of the header and part of
// what load verifies.
//
// The bitfield went with it because a `bool : 1` is a layout the standard leaves to the
// implementation, and this struct is written to a file with fwrite and read back by a
// possibly different compiler. A plain uint32 of flags costs the same and cannot drift.
#define MMAP_VERSION 8

enum MmapTileFlags
{
    MMAP_TILE_USES_LIQUIDS = 0x1    ///< the bake found liquid geometry in this tile
};

struct MmapTileHeader
{
    uint32 mmapMagic;
    uint32 dtVersion;
    uint32 mmapVersion;
    uint32 size;
    uint32 polyRefSize;   ///< sizeof(dtPolyRef) when this tile was baked
    uint32 flags;         ///< MmapTileFlags

    MmapTileHeader() : mmapMagic(MMAP_MAGIC), dtVersion(DT_NAVMESH_VERSION),
        mmapVersion(MMAP_VERSION), size(0),
        polyRefSize(uint32(sizeof(dtPolyRef))), flags(MMAP_TILE_USES_LIQUIDS) {}
};

enum NavTerrain
{
    NAV_EMPTY   = 0x00,
    NAV_GROUND  = 0x01,
    NAV_MAGMA   = 0x02,
    NAV_SLIME   = 0x04,
    NAV_WATER   = 0x08,
    NAV_UNUSED1 = 0x10,
    NAV_UNUSED2 = 0x20,
    NAV_UNUSED3 = 0x40,
    NAV_UNUSED4 = 0x80
    // we only have 8 bits
};

/**
 * @brief The polygon flags a baked surface of the given area carries.
 *
 * Detour keeps two independent per-polygon fields and asks a different question of
 * each. `dtPoly::area` is an INDEX (0..DT_MAX_AREAS) and its only consumer is
 * dtQueryFilter::getCost, which multiplies the segment length by m_areaCost[area] --
 * it answers "how expensive is this surface". `dtPoly::flags` is a BITMASK tested
 * against the filter's include/exclude masks -- it answers "may this mover be here at
 * all". Cost and permission are not the same question, and only the second one can be
 * expressed by refusing the polygon.
 *
 * The mapping is the identity today because the NAV_* values are single bits and
 * therefore serve as both. Naming it anyway is the point: it is the one place a future
 * area that is not its own permission bit -- a road that is merely cheaper than the
 * ground beside it, a steep slope that is merely dearer -- gets written down, and it
 * stops `flags = areas` from reading like the two fields are the same thing.
 */
inline uint16 NavAreaToFlags(unsigned char area)
{
    return uint16(area);
}

#endif  // _MOVE_MAP_SHARED_DEFINES_H
