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

#ifndef MANGOS_BOTSNG_PERCEPT_H
#define MANGOS_BOTSNG_PERCEPT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * What a bot knows at one instant, and nothing else.
 *
 * NOTHING IN THIS FILE INCLUDES A WORLD HEADER, and that is the whole design
 * rather than tidiness. The old engine read the world through 89 named `Value`
 * objects, each of which queried live state whenever a rule looked at it -- so
 * a decision could observe a target that was alive in one clause and dead in
 * the next, no decision could be reproduced, and none could be tested without
 * standing a server up.
 *
 * Here the world is read ONCE per tick into this struct, the struct is `const`
 * for the whole of the decision, and the decision is a pure function of it.
 * Three things follow, and each was impossible before:
 *
 *   * a test is a literal. `Perception p; p.self.healthPct = 12;` and the
 *     assertion is on what came out. src/tests/BotDecideTest.cpp compiles this
 *     directory and links none of the server.
 *   * a decision can be recorded and replayed. The snapshot IS the input, so a
 *     regression is a stored pair, not a story about what the bot was doing.
 *   * the decision touches nothing, so many bots can decide at once. Only
 *     sensing and executing run on the world thread -- which is what keeps
 *     `mangosd` the sole authority over game state.
 *
 * IDENTITY IS A NUMBER HERE. `EntityId` is an ObjectGuid's raw value and the
 * decision layer never dereferences it: it has no pointer to dangle when a
 * target despawns between the snapshot and the execution, which is a class of
 * bug rather than an instance of one.
 *
 * CAPACITIES ARE FIXED, so a Perception allocates nothing. The arrays cost
 * about three kilobytes together; the scratch is reused every tick, and three
 * kilobytes memcpy'd is cheaper than one of the grid searches this replaces.
 */
namespace bots
{
    /// An ObjectGuid's raw value. Never dereferenced in this layer.
    using EntityId = std::uint64_t;

    constexpr EntityId NoEntity = 0;

    /// Bounds. Raid size for allies; the rest are what one bot can usefully
    /// consider in a tick, not what the world can contain.
    constexpr std::size_t MaxAuras     = 24;
    constexpr std::size_t MaxCooldowns = 16;
    constexpr std::size_t MaxCarried   = 8;
    constexpr std::size_t MaxAllies    = 40;
    constexpr std::size_t MaxFoes      = 24;

    enum class PowerKind : std::uint8_t
    {
        Mana      = 0,
        Rage      = 1,
        Focus     = 2,
        Energy    = 3,
        Happiness = 4,
        None      = 5
    };

    /**
     * What a carried item is FOR, decided while sensing.
     *
     * The old actions each re-scanned the bags looking for their own item by
     * name -- "healing potion" parsed the item's name for the word potion, at
     * the moment it wanted one. Classifying once, here, means a layer asks for
     * a category and never learns an item id, so a server with different
     * consumables needs no new layer.
     */
    enum class Use : std::uint8_t
    {
        None       = 0,
        Heal       = 1,   ///< restores health at once
        Mana       = 2,   ///< restores power at once
        Food       = 3,   ///< restores health over time, out of combat
        Drink      = 4,   ///< restores power over time, out of combat
        Bandage    = 5,
        Healthstone = 6   ///< its own cooldown, which is why it is not Heal
    };

    struct Aura
    {
        std::uint32_t spell  = 0;
        std::uint32_t msLeft = 0;
        std::uint8_t  stacks = 0;
        bool          harmful = false;
    };

    struct Cooldown
    {
        std::uint32_t spell  = 0;
        std::uint32_t msLeft = 0;
    };

    struct Carried
    {
        std::uint32_t itemEntry     = 0;
        std::uint32_t count         = 0;
        std::uint32_t cooldownLeftMs = 0;
        std::uint32_t minLevel      = 0;   ///< the level the item is meant for
        Use           use           = Use::None;
    };

    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /**
     * Why the last thing the bot tried did not happen.
     *
     * THIS ONE FIELD IS THE WHOLE OF THE FEEDBACK PATH, and it replaces an
     * entire mechanism. An engine whose proposals can be refused needs some way
     * to not propose the same refused thing for ever, and the usual answer is a
     * queue of pending actions with alternatives to fall back to and a retry
     * that ages out -- machinery whose state outlives the tick that created it
     * and which nobody can then reason about.
     *
     * A refusal is information, so it belongs in the snapshot like every other
     * piece of information. One slot, overwritten by the next refusal, carried
     * into the next tick's Perception. A layer that cares looks at it; a layer
     * that does not is unaffected; nothing accumulates. "The server told me
     * that was out of range 200 ms ago" is a fact about the world, not a
     * control-flow structure.
     */
    enum class Refusal : std::uint8_t
    {
        None = 0,
        OutOfRange,
        NoLineOfSight,
        NotInFront,
        NotReady,        ///< cooldown or global cooldown
        NoPower,
        Immune,
        BadTarget,
        Moving,
        Interrupted,
        Unknown
    };

    struct Rebuff
    {
        std::uint32_t what = 0;   ///< the spell id or item entry refused
        std::uint32_t atMs = 0;   ///< when, on the world clock
        Refusal       why  = Refusal::None;
    };

    /**
     * Thresholds the layers read, carried in the snapshot on purpose.
     *
     * A layer is a plain function pointer -- no state, no allocation, nothing
     * to construct per bot -- so anything that varies per bot has to arrive
     * with the snapshot. That is not a workaround: a threshold IS something
     * the bot knows about itself, and putting it here means a test states it
     * in the same literal as the health it applies to.
     */
    struct Tuning
    {
        std::uint8_t healthstoneAt = 30;  ///< health %, in combat
        std::uint8_t healPotionAt  = 25;
        std::uint8_t manaPotionAt  = 15;
        std::uint8_t eatAt         = 60;  ///< health %, out of combat
        std::uint8_t drinkAt       = 50;  ///< power %, out of combat
        std::uint8_t healAllyAt    = 70;
        std::uint8_t emergencyAt   = 35;
    };

    struct Self
    {
        EntityId  guid   = NoEntity;
        std::uint8_t level = 1;
        std::uint8_t healthPct = 100;
        std::uint8_t powerPct  = 100;
        std::uint32_t health = 0;
        std::uint32_t maxHealth = 0;
        std::uint32_t power = 0;
        PowerKind powerKind = PowerKind::Mana;

        /// Milliseconds until the global cooldown lets another spell start.
        /// Zero means now. The old engine had no such number in front of the
        /// decision at all -- it cast, was refused, and pushed an alternative.
        std::uint32_t gcdLeftMs = 0;

        /// The spell being cast, when one is. A decision that ignores this
        /// interrupts the bot's own cast, which is how a caster ends up
        /// channelling nothing for a whole fight.
        std::uint32_t castingSpell = 0;

        bool inCombat = false;
        bool dead     = false;
        bool mounted  = false;
        bool moving   = false;
        bool eating   = false;
        bool drinking = false;
        bool stunned  = false;   ///< or feared, or otherwise not taking orders

        Point at;
        float facing = 0.0f;

        std::array<Aura, MaxAuras>         auras{};
        std::size_t                        auraCount = 0;
        std::array<Cooldown, MaxCooldowns> cooldowns{};
        std::size_t                        cooldownCount = 0;
        std::array<Carried, MaxCarried>    carried{};
        std::size_t                        carriedCount = 0;
    };

    struct Ally
    {
        EntityId     guid      = NoEntity;
        std::uint8_t level     = 1;
        std::uint8_t healthPct = 100;
        std::uint8_t powerPct  = 100;
        float        distance  = 0.0f;
        bool         dead      = false;
        bool         inCombat  = false;
        bool         isMaster  = false;
        bool         isTank    = false;
        bool         inLineOfSight = true;
    };

    struct Foe
    {
        EntityId     guid      = NoEntity;
        std::uint8_t level     = 1;
        std::uint8_t healthPct = 100;
        float        distance  = 0.0f;
        bool         casting   = false;
        bool         attackingMe = false;
        bool         isPlayer  = false;
        bool         inLineOfSight = true;
    };

    /**
     * One bot, one tick, everything the decision may look at.
     *
     * `tick` and `nowMs` come from the world's own clock rather than
     * `time(0)`: the old engine dated its decisions to the second, which is
     * coarser than the global cooldown it was deciding around.
     */
    struct Perception
    {
        std::uint64_t tick  = 0;
        std::uint32_t nowMs = 0;

        Tuning tune;
        Self   self;
        Rebuff lastRefusal;

        std::optional<Foe> target;   ///< what the bot is set on, if anything

        std::array<Ally, MaxAllies> allies{};
        std::size_t                 allyCount = 0;
        std::array<Foe, MaxFoes>    foes{};
        std::size_t                 foeCount = 0;

        /// True when sensing had to truncate. A layer must not silently take a
        /// short list for a small world: the healer that sees 40 of 41 raid
        /// members is fine, the one that sees 24 of 300 attackers is not.
        bool alliesTruncated = false;
        bool foesTruncated   = false;
    };

    /// Whether @a spell is on cooldown for this bot right now.
    inline std::uint32_t CooldownLeft(Self const& self, std::uint32_t spell)
    {
        for (std::size_t i = 0; i < self.cooldownCount; ++i)
        {
            if (self.cooldowns[i].spell == spell)
            {
                return self.cooldowns[i].msLeft;
            }
        }
        return 0;
    }

    /// The aura @a spell, when this bot is under it.
    inline Aura const* FindAura(Self const& self, std::uint32_t spell)
    {
        for (std::size_t i = 0; i < self.auraCount; ++i)
        {
            if (self.auras[i].spell == spell)
            {
                return &self.auras[i];
            }
        }
        return nullptr;
    }

    /**
     * The best carried item of category @a use that is ready to use.
     *
     * "Best" is the highest `minLevel` the bot can actually use, which is what
     * makes a level 60 drink the level 55 water rather than the starter one it
     * has been carrying since Elwynn. An item still on its cooldown is not a
     * candidate at all -- proposing it and being refused is exactly the retry
     * loop this design exists to delete.
     */
    inline Carried const* BestCarried(Self const& self, Use use)
    {
        Carried const* best = nullptr;
        for (std::size_t i = 0; i < self.carriedCount; ++i)
        {
            Carried const& item = self.carried[i];
            if (item.use != use || item.count == 0 || item.cooldownLeftMs > 0)
            {
                continue;
            }
            if (item.minLevel > self.level)
            {
                continue;
            }
            if (!best || item.minLevel > best->minLevel)
            {
                best = &item;
            }
        }
        return best;
    }
}

#endif //MANGOS_BOTSNG_PERCEPT_H
