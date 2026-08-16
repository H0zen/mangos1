#pragma once

// WHAT a navigation cell is made of, and what a mover is allowed to do with it.
//
// Two vocabularies that have to be defined together, because the only question the
// router ever asks of a cell is whether THIS mover may stand on it and what it costs
// him to cross it. Keeping them in one header is what stops a new surface from being
// added to the baker and silently matching nobody's permission mask.
//
// == Areas are indices, permissions are bits ==
//
// A cell carries an AREA -- one small number naming the surface: ground, shallow water,
// deep water, magma. The mover carries a MASK of the areas he may occupy plus a COST
// per area. The two are separate for the reason the old Detour layer discovered the
// hard way: "may I be here" and "how dear is it here" are different questions, and only
// the first can be answered by refusing the cell. A road is cheaper than the grass
// beside it and forbidden to nobody.
//
// Four bits, so an area packs beside its flags in one byte. Fifteen surfaces and a
// sixteenth that means "not walkable" is more than 2.4.3 distinguishes.

#include <cstdint>

namespace Nav
{
    /**
     * @brief The surface under one cell.
     *
     * Ordered so that Blocked is zero: a zeroed cell is an unwalkable cell, which is
     * what a partially written tile, a cleared buffer and an out-of-range read all
     * produce. Every accident lands on "you may not go there".
     */
    enum class NavArea : uint8_t
    {
        Blocked = 0,   ///< nothing walkable here
        Ground  = 1,   ///< solid floor: terrain, a building's floor, a bridge
        Shallow = 2,   ///< liquid a walker wades through, feet still on the floor
        Water   = 3,   ///< liquid deep enough to swim in
        Magma   = 4,
        Slime   = 5,

        Count   = 6
    };

    constexpr uint8_t AREA_BITS = 4;
    constexpr uint8_t AREA_MASK = 0x0F;

    /// The permission bit that stands for one area.
    constexpr uint16_t AreaBit(NavArea area)
    {
        return uint16_t(1u << uint8_t(area));
    }

    /// Every surface with a floor a walker's feet can reach.
    constexpr uint16_t AREAS_WALKABLE =
        AreaBit(NavArea::Ground) | AreaBit(NavArea::Shallow);

    /// Every surface a body can be immersed in.
    constexpr uint16_t AREAS_LIQUID =
        AreaBit(NavArea::Shallow) | AreaBit(NavArea::Water) |
        AreaBit(NavArea::Magma) | AreaBit(NavArea::Slime);

    /// Liquid that hurts. Nothing routes through it that is not immune, and nothing
    /// this server knows about is.
    constexpr uint16_t AREAS_HARMFUL =
        AreaBit(NavArea::Magma) | AreaBit(NavArea::Slime);

    /**
     * @brief What is true about a cell beyond the surface it is made of.
     *
     * Four bits, packed in the high nibble of the same byte as the area.
     */
    enum NavCellFlag : uint8_t
    {
        /// This cell has more walkable surfaces above the one in the dense plane.
        /// Set by the baker; the only reason to consult the sparse layer table.
        CELL_STACKED = 0x10,

        /// At least one 8-neighbour is not reachable from here: an edge, a ledge, the
        /// lip of a drop. Routing prefers to keep off these when it can, so creatures
        /// do not walk the very rim of a cliff.
        CELL_BORDER = 0x20,

        /// The surface is steep enough that a mover slides rather than walks. Baked as
        /// walkable because the geometry is there, but priced high.
        CELL_STEEP = 0x40,

        /// The floor under this cell is a model posed at bake time -- a building, a
        /// bridge -- rather than the ADT heightmap. Kept because a door or a lift can
        /// move at runtime and the router must be able to say what it is standing on.
        CELL_MODEL = 0x80
    };

    constexpr uint8_t FLAG_MASK = 0xF0;

    /// Pack an area and its flags into the one byte the dense plane stores.
    constexpr uint8_t PackArea(NavArea area, uint8_t flags)
    {
        return uint8_t((uint8_t(area) & AREA_MASK) | (flags & FLAG_MASK));
    }

    constexpr NavArea AreaOf(uint8_t packed)
    {
        return NavArea(packed & AREA_MASK);
    }

    constexpr uint8_t FlagsOf(uint8_t packed)
    {
        return uint8_t(packed & FLAG_MASK);
    }

    constexpr bool Walkable(uint8_t packed)
    {
        return AreaOf(packed) != NavArea::Blocked;
    }

    /**
     * @brief How much room a cell has around it, in yards.
     *
     * Stored per cell by the baker as the distance to the nearest unwalkable cell, and
     * it is what lets ONE bake serve every creature. The polygon mesh this replaces
     * eroded the walkable surface by a single agent radius chosen at bake time, so a
     * murloc and a devilsaur were routed through identically sized doorways and the
     * only way to serve both was to bake twice. Here the radius is a term in the
     * QUERY: the mover's own radius is compared against what each cell recorded.
     *
     * Eighths of a yard, saturating, so 31.875 yards reads as "open ground" -- past any
     * radius 2.4.3 has.
     */
    constexpr float CLEARANCE_QUANTUM = 0.125f;

    inline uint8_t QuantiseClearance(float yards)
    {
        const float steps = yards / CLEARANCE_QUANTUM;

        // `!(steps > 0)` rather than `steps <= 0`: the two read the same and differ on
        // NaN, which is the one value that must not reach the cast below -- converting
        // it to uint8_t is undefined behaviour, not a large number.
        if (!(steps > 0.0f))
        {
            return 0;
        }
        if (steps >= 255.0f)
        {
            return 255;
        }
        return static_cast<uint8_t>(steps);
    }

    inline float RestoreClearance(uint8_t q)
    {
        return float(q) * CLEARANCE_QUANTUM;
    }

    /**
     * @brief What one mover is permitted to do, as a value.
     *
     * The router reads this and never asks the unit itself, which is the whole of the
     * separation between deciding and routing: a permission that is a field can be
     * constructed in a test, while a permission that is a call into a live Unit can
     * only be observed by standing a Unit up, which needs a Map, which needs the
     * database.
     *
     * Snapshotted once per routing request by the game layer -- see Nav::ProfileOf --
     * because several of these depend on where the mover is standing at this instant.
     */
    struct MoveProfile
    {
        /// Areas this mover may occupy, as AreaBit() bits.
        uint16_t allowedAreas = AREAS_WALKABLE;

        /// Multiplier on the length of a segment crossing each area. Index by
        /// uint8_t(NavArea). One is neutral; larger makes the router go round.
        float areaCost[uint8_t(NavArea::Count)] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};

        /**
         * @brief How wide the mover is, in yards.
         *
         * Compared against each cell's recorded clearance. Zero admits every walkable
         * cell, which is what a query that does not care about width wants.
         */
        float radius = 0.0f;

        /// The tallest step up the mover takes without it being a jump, in yards.
        float maxClimb = 1.0f;

        bool canSwim = false;
        bool canFly = false;

        /**
         * @brief This mover rides the water rather than the bottom of it.
         *
         * INHABIT DECIDES, and ground wins. A creature whose InhabitType carries
         * GROUND walks -- on the sand, on the seabed, under thirty yards of ocean if
         * that is where the floor is. A makrura does not swim. A crab does not swim.
         * Having WATER as well only means the water does not stop them.
         *
         * The exception is a pet, which follows a player wherever the player goes,
         * including out over water too deep for any floor to matter. It is the only
         * thing in the game that has feet and still has to be given the surface.
         *
         * So this is not `canSwim` (an ability every second creature has) and not "is
         * currently wet" (a situation) -- it is a small, named exception, and it stays
         * small.
         */
        bool ridesWater = false;

        /**
         * @brief Does this mover have feet on the floor?
         *
         * A creature that walks stays on the floor even when the floor is a seabed -- a
         * crab crosses a bay along the bottom, it does not surface halfway. The baked
         * data carries the liquid surface and the seabed as two stacked layers;
         * AdmitsGround keeps a walker on the floor.
         */
        bool canWalk = false;

        /**
         * @brief May this mover travel where the baked data describes no ground?
         *
         * Creatures may; players never do. A client drives its own movement and would be
         * desynchronised by a server route through geometry it can walk into, so the
         * exemption was always for creatures.
         */
        bool mayLeaveMesh = false;

        /// The mover is exempt from routing entirely.
        bool ignorePathfinding = false;

        /// May the mover stand on a cell carrying this area?
        bool Admits(NavArea area) const
        {
            return (allowedAreas & AreaBit(area)) != 0;
        }

        bool Admits(uint8_t packed) const { return Admits(AreaOf(packed)); }

        /**
         * @brief May the mover stand on this ground -- the WHOLE permission, one copy.
         *
         * `Admits` is the mask alone, and the mask alone has never been the answer: a
         * creature that walks stays on the floor even where the floor is a seabed, so
         * the surface of a bay is offered to it by the data and refused to it by this.
         *
         * It lives here rather than in whichever search asked first because it was in
         * two places and the two disagreed. The cell engine applied the swimmer rule and
         * the mesh did not, so the same walker was routed across a bay by one and along
         * the bottom of it by the other -- one question, one answer, and this is where
         * the answer is.
         */
        bool AdmitsGround(uint8_t packed) const
        {
            const NavArea area = AreaOf(packed);
            if (!Admits(area))
            {
                return false;
            }

            // GROUND WINS, and the data says so plainly. InhabitType is where a
            // creature lives: a Surf Crawler is GROUND, a threshadon is WATER, a
            // makrura is GROUND|WATER -- and a makrura walks. Carrying the water bit
            // means the water does not stop it, not that it swims. `canWalk` here IS
            // `InhabitType & INHABIT_GROUND`; nothing else needs asking.
            //
            // Admitting a ground-dweller to the skin as well is what makes it hop:
            // two stacked layers, a seat chosen per point, and a body that alternates
            // between the sand and the surface all the way across a bay. Watched, on
            // Darkspear Strand, on a Makrura Shellhide -- InhabitType 3.
            //
            // The exception is small and stays small. A pet follows its owner out over
            // water no floor can reach, and a player is client-driven anyway. Those
            // ride the water. Everything with feet walks on the bottom.
            return !(area == NavArea::Water && canWalk && !ridesWater);
        }

        /// The multiplier for crossing an area.
        float CostOf(NavArea area) const
        {
            return areaCost[uint8_t(area) < uint8_t(NavArea::Count) ? uint8_t(area) : 0];
        }

        /**
         * @brief What crossing this ground costs, as a multiplier on distance.
         *
         * Above one for ground that is passable and unpleasant. Steep ground is priced
         * rather than refused, because the geometry really is walkable and a creature
         * that refused every slope would stand at the bottom of hills it can climb.
         */
        float PenaltyOf(uint8_t packed) const
        {
            float cost = CostOf(AreaOf(packed));
            if (FlagsOf(packed) & CELL_STEEP)
            {
                cost *= 2.0f;
            }
            return cost;
        }

        /// Is this cell wide enough for the mover?
        bool Fits(uint8_t clearance) const
        {
            return radius <= 0.0f || RestoreClearance(clearance) >= radius;
        }

        /**
         * @brief May this mover cross ground the baked data does not describe?
         *
         * The geometry has already answered "not on the mesh"; this decides whether that
         * is fatal, or merely means the mover swims or flies over it.
         */
        bool MayGoDirect(bool underWater) const
        {
            return mayLeaveMesh && (underWater ? canSwim : canFly);
        }
    };
}
