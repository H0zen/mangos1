#pragma once

// The one place a baked navigation tile becomes bytes, and the one place bytes become a
// tile again. Writer and reader sit in the same file so a section cannot be added to one
// and forgotten in the other.
//
// Native-endian, same-machine cache -- the same contract terrain tiles are written
// under, and for the same reason: this is a build artefact of the extractor that ran on
// the box, not a portable archive. Magic and version guard it, and the GRID PARAMETERS
// are part of what is verified: a tile baked at a different cell size or a different
// vertical quantum parses perfectly and is wrong everywhere, so it is refused rather
// than read.
//
// A refused tile is silence -- no ground, no regions, no route through it -- which is
// the loud kind of failure. It is far better than a tile that answers with a grid one
// step out of phase.

#include "nav/NavTile.hpp"

#include <cstdint>
#include <string>

namespace Nav
{
    /**
     * @brief Bumped whenever the byte layout OR the meaning of a field changes.
     *
     * A BUMP REQUIRES A FULL RE-BAKE before the branch is served. A stale tile is
     * refused, and a refused tile is silence: no ground, no route through it, and the
     * movement code falls back to straight lines over that square of the world.
     *
     * Version 2 adds the cell and layer a gateway stands on, and a table of
     * hand-authored links -- the jump off the Booty Bay dock and the two ledges in
     * Blade's Edge Arena.
     */
    constexpr uint32_t NAV_TILE_VERSION = 3;

    /// The file one tile of one map lives in, relative to the nav directory.
    std::string NavTileFileName(uint32_t mapId, int tileX, int tileY);

    /// Where the server looks for nav tiles. Set once at start-up.
    void SetNavDir(const std::string& dir);
    const std::string& NavDir();

    /// True when a nav tile file exists for this map cell. Cheap: it stats, it does not
    /// parse. Used to tell "this map has no navigation at all" from "this bake is
    /// broken", which are answered very differently.
    bool HasNavTile(uint32_t mapId, int tileX, int tileY);

    /**
     * @brief Write one tile.
     * @param path Full path of the file to create.
     * @return False on any short write; the caller must treat a false as a failed bake
     *         and not leave the partial file where a server would read it.
     */
    bool WriteNavTile(const std::string& path, uint32_t mapId, const NavTile& tile);

    /**
     * @brief Read one tile.
     *
     * Every count is checked against the bytes actually remaining in the file before it
     * is used to size anything, so a corrupt header cannot make the reader reserve
     * hundreds of megabytes it will never fill.
     *
     * @return False when the file is missing, stale, or does not parse. `out` is left
     *         empty in every failing case -- never half-filled.
     */
    bool ReadNavTile(const std::string& path, uint32_t mapId, NavTile& out);
}
