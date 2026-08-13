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

#ifndef MANGOS_BOTSNG_INTENT_H
#define MANGOS_BOTSNG_INTENT_H

#include "Percept.h"

#include <cstdint>
#include <type_traits>
#include <variant>

/**
 * What a bot WANTS to do, said as a value.
 *
 * The old engine had 503 classes deriving from `Action`, each of which decided
 * and then did the thing itself, inside `Execute`. Deciding and doing in the
 * same virtual call is what tied every rule to a live `Player*`, and with it
 * went any hope of testing a rule, logging what was chosen and why, or letting
 * two bots decide at the same time.
 *
 * Here a layer PROPOSES an Intent and never acts. One executor applies it,
 * with a single `std::visit`. The verbs are a closed set on purpose: a bot can
 * cast, use an item, move, follow, swing and flee, and that really is all a
 * client can ask a character to do.
 *
 * THE COST IS PART OF THE PROPOSAL, and it is the idea this whole design turns
 * on. A tick has budgets -- one global cooldown, one cast, one movement order,
 * whatever power is in the bar -- and a proposal that states what it spends can
 * be weighed against the others before any of them happens. The old queue
 * discovered the cost by trying: it popped the best action, called
 * `isPossible()`, was refused, pushed an alternative at relevance + 0.03 and
 * tried again next tick. Every one of prerequisites, alternatives, continuers
 * and expiry exists to recover from a choice that should never have been made.
 */
namespace bots
{
    /**
     * Urgency, absolute rather than relative.
     *
     * A layer states how much it matters that this happens, on a scale it
     * shares with every other layer, and NOTHING multiplies it afterwards. The
     * old `Multiplier` chain let any strategy scale any action's relevance,
     * which meant the ordering was an emergent property of which strategies
     * happened to be loaded -- and the bands below are the thing that was
     * missing: a number nobody can read is a number nobody can debug.
     *
     *   900+  I am about to die
     *   700   an emergency that is not death (an ally at 15%, a fear to break)
     *   500   the rotation's important ability
     *   300   maintenance out of combat (eat, drink, buff)
     *   100   filler
     *     0   proposed but indifferent
     *    -1   never; the arbiter drops it
     */
    using Score = std::int32_t;

    constexpr Score ScoreNever     = -1;
    constexpr Score ScoreFiller    = 100;
    constexpr Score ScoreUpkeep    = 300;
    constexpr Score ScoreRotation  = 500;
    constexpr Score ScoreEmergency = 700;
    constexpr Score ScoreSurvival  = 900;

    // -- the verbs -----------------------------------------------------------

    /// Nothing, said explicitly. A layer that has looked and found nothing to
    /// do proposes this rather than staying silent, so a trace distinguishes
    /// "considered and declined" from "never ran".
    struct Idle
    {
    };

    struct Cast
    {
        std::uint32_t spell  = 0;
        EntityId      target = NoEntity;   ///< NoEntity means the bot itself
    };

    struct UseItem
    {
        std::uint32_t itemEntry = 0;
        EntityId      target    = NoEntity;
    };

    struct MoveTo
    {
        Point to;
        bool  run = true;
    };

    struct Follow
    {
        EntityId whom     = NoEntity;
        float    distance = 1.5f;
    };

    struct AttackMelee
    {
        EntityId whom = NoEntity;
    };

    struct Flee
    {
        EntityId from     = NoEntity;
        float    distance = 15.0f;
    };

    using Verb = std::variant<Idle, Cast, UseItem, MoveTo, Follow, AttackMelee,
                              Flee>;

    /**
     * The one thing at a time a verb occupies.
     *
     * Three channels, because a character really can move, swing and cast an
     * instant in the same moment. Modelling all of it as one "action slot" is
     * what forces a rotation to alternate walking and casting for no reason
     * the game imposes; modelling none of it is what lets two layers each cast
     * a different spell in the same global cooldown.
     */
    enum class Channel : std::uint8_t
    {
        None     = 0,   ///< Idle occupies nothing
        Action   = 1,   ///< a cast or an item use
        Movement = 2,   ///< where the body goes
        Target   = 3    ///< what it is swinging at
    };

    /**
     * Which channel @a verb occupies, derived rather than declared.
     *
     * A proposal cannot get this wrong, because it never states it. The
     * alternative -- two booleans on the cost, filled in by each layer -- is a
     * field that can disagree with the verb beside it, and a `Cast` that
     * claims not to use the action channel is a bot casting twice per global
     * cooldown, found by a player rather than by us.
     */
    inline Channel ChannelOf(Verb const& verb)
    {
        return std::visit([](auto const& v) -> Channel
        {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, Cast> || std::is_same_v<V, UseItem>)
            {
                return Channel::Action;
            }
            else if constexpr (std::is_same_v<V, MoveTo> ||
                               std::is_same_v<V, Follow> ||
                               std::is_same_v<V, Flee>)
            {
                return Channel::Movement;
            }
            else if constexpr (std::is_same_v<V, AttackMelee>)
            {
                return Channel::Target;
            }
            else
            {
                return Channel::None;
            }
        }, verb);
    }

    /**
     * What a verb spends out of the tick, beyond its channel.
     *
     * Only two things are consumable rather than exclusive: the global
     * cooldown and the power bar. Everything else about "can I" is a question
     * about the snapshot, and a proposal that has to ask it has already been
     * answered -- that is why there is no `possible` flag here.
     */
    struct Budget
    {
        std::uint32_t gcdMs = 0;
        std::uint32_t power = 0;
    };

    struct Intent
    {
        Verb   verb;
        Score  score = 0;
        Budget cost;

        /// Why this was proposed, for the trace and for a failing test's
        /// message. A string literal, never owned and never parsed -- the
        /// decision layer allocates nothing, and that includes its diagnostics.
        char const* why = "";
    };

    /// A cast that spends the global cooldown and @a power, which is what
    /// nearly every proposal wants and nobody should spell out by hand.
    inline Intent ProposeCast(std::uint32_t spell, EntityId target, Score score,
                              std::uint32_t power, char const* why,
                              std::uint32_t gcdMs = 1500)
    {
        Intent intent;
        intent.verb       = Cast{spell, target};
        intent.score      = score;
        intent.cost.gcdMs = gcdMs;
        intent.cost.power = power;
        intent.why        = why;
        return intent;
    }

    /// Using an item takes the action channel but no power, and in 2.4.3 no
    /// global cooldown either -- a potion and an instant in the same second is
    /// how the fight is actually played.
    inline Intent ProposeUse(std::uint32_t itemEntry, EntityId target,
                             Score score, char const* why)
    {
        Intent intent;
        intent.verb  = UseItem{itemEntry, target};
        intent.score = score;
        intent.why   = why;
        return intent;
    }
}

#endif //MANGOS_BOTSNG_INTENT_H
