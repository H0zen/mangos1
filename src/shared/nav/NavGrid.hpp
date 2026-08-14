#pragma once

// WHERE a navigation cell is. Nothing else -- no data, no search, no files.
//
// The baker and the server both include this, and that is the whole point: a cell
// index computed by the writer and a cell index computed by the reader are the same
// arithmetic or the tile is read shifted. It is header-only and constexpr so the two
// cannot even link against different builds of it.
//
// == The grid is the terrain grid, refined ==
//
// TerrainInfo bins a world coordinate with
//
//     GridCoord(c) = GRID_PER_TILE * (MAP_CENTER - c / TILE_SIZE)      // 128 per tile
//
// and takes the tile from `int(floor(GridCoord)) >> 7`. This grid is that one times
// four:
//
//     CellCoord(c) = CELLS_PER_TILE * (MAP_CENTER - c / TILE_SIZE)     // 512 per tile
//
// so CellCoord == 4 * GridCoord exactly, and `cell >> 9` is `grid >> 7` -- the same
// tile, by construction rather than by agreement. Four nav cells span one terrain
// height cell, so a liquid or area lookup off the ADT grid is an integer divide and
// never a resample.
//
// 512 is a power of two on purpose: the tile index is a shift and the in-tile index a
// mask, which a search doing this per neighbour visit notices.
//
// == Axis direction ==
//
// Cell indices grow as world coordinates FALL, inherited from the terrain grid. Every
// piece of this module obeys it; the one place it is easy to get wrong is stitching
// tiles, where the neighbour at cellX - 1 lives in the tile at larger world x.

#include "terrain/Terrain.hpp"

#include <cmath>
#include <cstdint>

namespace Nav
{
    /// Nav cells along one edge of a tile. A power of two; see the header comment.
    constexpr int CELLS_PER_TILE = 512;

    /// log2(CELLS_PER_TILE), so a global cell index splits with a shift and a mask.
    constexpr int CELL_SHIFT = 9;

    /// Nav cells per terrain height cell. Exact, and required to be.
    constexpr int CELLS_PER_HEIGHT_CELL =
        CELLS_PER_TILE / world::terrain::GRID_PER_TILE;

    /// The world size of one cell, in yards.
    constexpr float CELL_SIZE = world::terrain::TILE_SIZE / float(CELLS_PER_TILE);

    /**
     * @brief The vertical distance a step of `length` yards across the grid may cover.
     *
     * A climb limit and a slope limit are different quantities and the bake used only
     * the first. `maxClimb` is a STEP -- a kerb, a stair, the lip of a balcony -- while
     * a hillside is a grade, and one cell of grade is 1.04 yards long. At 50 degrees,
     * inside the 55 the bake calls walkable, that cell rises
     *
     *     1.0417 * tan(50 deg) = 1.24 yards
     *
     * which is over a one-yard step, so no neighbour ever linked to another. Every cell
     * of the bank became a region of one node and `MIN_REGION_NODES` swept the hill
     * away, while `maxSlopeDeg` -- the limit written to judge exactly this -- never saw
     * a link steep enough to refuse, because nothing above 43.8 degrees could be linked
     * in the first place. Both constants were dead letters, in opposite directions.
     *
     * The window is whichever is larger: the step the mover can climb, or the rise the
     * slope limit itself permits over that distance. Ground steeper than the limit
     * still links to nothing and is still dropped -- which is what refusing it means.
     *
     * The bake, the tile stitcher and the fine search must all use this one function.
     * They already had to agree: a query allowed to step further than the flood that
     * built the regions walks between two cells the coarse stage believes are only
     * joined through a gateway, and the disagreement surfaces as the occasional route
     * that ignores a door.
     */
    inline float ClimbWindow(float maxClimb, float maxSlopeDeg, float length)
    {
        const float rise = length * std::tan(maxSlopeDeg * 3.14159265f / 180.0f);
        return maxClimb > rise ? maxClimb : rise;
    }

    /// Tiles along one edge of a map, mirroring the terrain grid.
    constexpr int TILES_PER_MAP = world::terrain::FusedTerrainGridCount;

    /// Cells along one edge of a whole map.
    constexpr int CELLS_PER_MAP = CELLS_PER_TILE * TILES_PER_MAP;

    /// Cells in one tile.
    constexpr int CELLS_PER_TILE_SQ = CELLS_PER_TILE * CELLS_PER_TILE;

    /**
     * @brief Vertical resolution of a stored surface, in yards.
     *
     * A surface height is kept as a uint16 count of these above the tile's base, so the
     * span a tile can describe is 65535 * this. At 1/16 yard that is 4095 yards, which
     * no single tile of any 2.4.3 map comes near, and 6 centimetres of error is an order
     * of magnitude below the step a creature can climb.
     */
    constexpr float Z_QUANTUM = 0.0625f;

    /// The tallest span one tile can describe, in yards.
    constexpr float Z_SPAN = 65535.0f * Z_QUANTUM;

    /**
     * @brief Global nav-cell coordinate of a world coordinate, unrounded.
     *
     * Deliberately the same shape as world::terrain::GridCoord, four times finer.
     */
    inline float CellCoord(float c)
    {
        return CELLS_PER_TILE * (float(world::terrain::MAP_CENTER) -
                                 c / world::terrain::TILE_SIZE);
    }

    /**
     * @brief Global nav-cell index of a world coordinate.
     *
     * Floors rather than truncates, and folds the far edge inwards, for the two reasons
     * world::terrain::TileIndex spells out at length: truncation sends the strip in
     * (-1, 0) to cell 0 instead of cell -1, and the exact far corner lands one past the
     * last cell. This grid is a refinement of that one, so it inherits both fixes rather
     * than re-deriving them.
     *
     * Out-of-map coordinates are returned as-is (negative, or >= CELLS_PER_MAP). The
     * caller tests; clamping here would answer a query off the map with the edge cell.
     */
    inline int CellIndex(float c)
    {
        const int g = static_cast<int>(std::floor(CellCoord(c)));
        return g == CELLS_PER_MAP ? CELLS_PER_MAP - 1 : g;
    }

    /// The tile a global cell index belongs to.
    constexpr int TileOfCell(int cell) { return cell >> CELL_SHIFT; }

    /// The in-tile index of a global cell index.
    constexpr int LocalOfCell(int cell) { return cell & (CELLS_PER_TILE - 1); }

    /// The global cell index a tile's local index sits at.
    constexpr int GlobalCell(int tile, int local)
    {
        return (tile << CELL_SHIFT) + local;
    }

    /// True when a global cell index lies on the map.
    constexpr bool OnMap(int cell)
    {
        return cell >= 0 && cell < CELLS_PER_MAP;
    }

    /**
     * @brief World coordinate of the CENTRE of a global cell index.
     *
     * The centre and not a corner: a cell stands for the ground under a square of
     * world, and every consumer that turns a cell back into a position -- the point a
     * route emits, the sample the baker takes -- means the middle of that square. Two
     * of them meaning different corners is a half-cell drift that only shows up as
     * creatures walking a hand's width inside walls.
     */
    inline float CellCentre(int cell)
    {
        return (float(world::terrain::MAP_CENTER) - (float(cell) + 0.5f) /
                float(CELLS_PER_TILE)) * world::terrain::TILE_SIZE;
    }

    /// The terrain height-cell index (0..GRID_PER_TILE-1) an in-tile nav cell sits in.
    constexpr int HeightCellOfLocal(int local)
    {
        return local / CELLS_PER_HEIGHT_CELL;
    }

    /**
     * @brief A cell address on one map: which tile, and where inside it.
     *
     * Carried as global indices, because every piece of arithmetic a search does --
     * stepping to a neighbour, measuring a distance, crossing into the next tile -- is
     * addition on the global index, and only the moment a tile's storage is touched
     * needs the split.
     */
    struct CellRef
    {
        int x = -1;   ///< global cell index along world X
        int y = -1;   ///< global cell index along world Y

        bool Valid() const { return OnMap(x) && OnMap(y); }

        int TileX() const { return TileOfCell(x); }
        int TileY() const { return TileOfCell(y); }
        int LocalX() const { return LocalOfCell(x); }
        int LocalY() const { return LocalOfCell(y); }

        /// Index into a tile's dense per-cell planes. Row-major in local X.
        int InTile() const { return LocalX() * CELLS_PER_TILE + LocalY(); }

        bool operator==(const CellRef& o) const { return x == o.x && y == o.y; }
        bool operator!=(const CellRef& o) const { return !(*this == o); }
    };

    /// The cell a world position falls in.
    inline CellRef CellAt(float worldX, float worldY)
    {
        CellRef ref;
        ref.x = CellIndex(worldX);
        ref.y = CellIndex(worldY);
        return ref;
    }

    /// Centre of a cell, in world coordinates.
    inline void CellCentre(const CellRef& ref, float& worldX, float& worldY)
    {
        worldX = CellCentre(ref.x);
        worldY = CellCentre(ref.y);
    }

    /// Quantise a world height against a tile's base. Saturates rather than wrapping.
    inline uint16_t QuantiseZ(float z, float tileBaseZ)
    {
        const float steps = (z - tileBaseZ) / Z_QUANTUM;
        if (steps <= 0.0f)
        {
            return 0;
        }
        if (steps >= 65535.0f)
        {
            return 65535;
        }
        return static_cast<uint16_t>(steps + 0.5f);
    }

    /// The world height a quantised value stands for.
    inline float RestoreZ(uint16_t q, float tileBaseZ)
    {
        return tileBaseZ + float(q) * Z_QUANTUM;
    }
}
