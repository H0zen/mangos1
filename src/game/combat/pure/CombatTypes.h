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

#ifndef MANGOS_COMBAT_TYPES_H
#define MANGOS_COMBAT_TYPES_H

#include <cstddef>
#include <cstdint>

/**
 * @brief The vocabulary of the pure combat core.
 *
 * Nothing in this directory includes a game header. Not a convention: the
 * combat_pure library links into the test binary without the game library,
 * and CheckCombatBoundary.cmake fails the suite if an include creeps in.
 * Everything a strike needs is either in a Profile or passed as an argument.
 */
namespace Combat
{
    /// Hundredths of a percent. The whole table lives in integers; there is no
    /// int32(chance * 100) scattered through the roll any more.
    using Hundredths = std::int32_t;

    /// A full probability, so a table always terminates.
    constexpr Hundredths HUNDRED_PERCENT = 10000;

    /// Which weapon swung. RANGED shares the pipeline but not the table.
    enum class Hand : std::uint8_t
    {
        Main   = 0,
        Off    = 1,
        Ranged = 2,
        Count  = 3
    };

    constexpr std::size_t HAND_COUNT = static_cast<std::size_t>(Hand::Count);

    /// Index of a hand, for the per-hand arrays in Profile.
    constexpr std::size_t Index(Hand hand)
    {
        return static_cast<std::size_t>(hand);
    }

    /**
     * @brief What kind of combatant this is, as far as the rules care.
     *
     * Not a game-object type. Glancing needs "player or pet attacking a
     * non-pet creature", crushing needs "creature that is not a pet", the
     * miss curve forks on "victim is a player". Those are three different
     * questions that the old code asked as TYPEID_PLAYER in three places.
     */
    enum class Kind : std::uint8_t
    {
        Player   = 0,
        Pet      = 1,
        Creature = 2
    };

    /**
     * @brief The outcome of one swing.
     *
     * The order is the roll order, and HitTable depends on it: the cumulative
     * bounds are indexed by this enum, so moving a member reorders the table.
     * Normal is last because it is the fallback, not a band.
     *
     * There is no BlockCrit. In 2.4.3 a special that is blocked rolls block
     * and crit separately (HitTable::TwoRoll), so a one-roll table has no
     * band it could occupy -- which is why the old MELEE_HIT_BLOCK_CRIT was
     * unreachable rather than merely unhandled.
     */
    enum class Outcome : std::uint8_t
    {
        Evade    = 0,
        Miss     = 1,
        Dodge    = 2,
        Parry    = 3,
        Glancing = 4,
        Block    = 5,
        Crit     = 6,
        Crushing = 7,
        Normal   = 8,
        Count    = 9
    };

    constexpr std::size_t OUTCOME_COUNT = static_cast<std::size_t>(Outcome::Count);

    constexpr std::size_t Index(Outcome outcome)
    {
        return static_cast<std::size_t>(outcome);
    }

    /// True when the swing put damage on the health bar path at all.
    constexpr bool Connects(Outcome outcome)
    {
        return outcome == Outcome::Normal || outcome == Outcome::Crit ||
               outcome == Outcome::Glancing || outcome == Outcome::Crushing ||
               outcome == Outcome::Block;
    }

    /// The victim answered rather than absorbed: no damage, but rage and
    /// reactive abilities still key off it.
    constexpr bool IsAvoidance(Outcome outcome)
    {
        return outcome == Outcome::Miss || outcome == Outcome::Dodge ||
               outcome == Outcome::Parry || outcome == Outcome::Evade;
    }

    /// Weapon damage, already resolved to integers. Rolling in floats and
    /// truncating with a cast is how min 0.9 / max 1.1 became urand(0, 1).
    struct DamageRange
    {
        std::uint32_t low  = 0;
        std::uint32_t high = 0;

        constexpr bool Empty() const
        {
            return high == 0;
        }
    };

    /**
     * @brief What a combatant is able to do, decided once instead of re-derived
     *        in the middle of the roll.
     *
     * "Every creature blocks at 5%" and "only humanoids parry" were not chosen;
     * they fell out of asking the question at roll time, where the only thing
     * in scope was the creature type. Here the builder answers it from the
     * equipment and the creature flags, and the table just reads a bit.
     */
    struct Caps
    {
        bool canDodge     = false;
        bool canParry     = false;   ///< a parry-capable weapon, or the flag
        bool canBlock     = false;   ///< an actual shield, or the flag
        bool mayCrush     = false;   ///< attacker side: not flagged NO_CRUSH
        bool mayGlance    = false;   ///< attacker side: not a special
        bool dualWielding = false;
    };

    /// The facts about this particular swing that are not properties of either
    /// combatant. Geometry arrives already resolved in the shared frame -- the
    /// core never composes a position, and on a vessel it never could.
    struct Situation
    {
        bool fromBehind    = false;
        bool victimSitting = false;
        bool victimEvading = false;
    };

    /// Physical. The pure core needs exactly one school constant: whether
    /// armour applies. Everything else about schools belongs to the caller.
    constexpr std::uint32_t SCHOOL_MASK_PHYSICAL = 0x01;

    constexpr bool IsPhysical(std::uint32_t schoolMask)
    {
        return (schoolMask & SCHOOL_MASK_PHYSICAL) != 0;
    }
}

#endif
