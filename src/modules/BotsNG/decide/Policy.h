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

#ifndef MANGOS_BOTSNG_POLICY_H
#define MANGOS_BOTSNG_POLICY_H

#include "Arbiter.h"
#include "IntentSink.h"
#include "Percept.h"

#include <vector>

namespace bots
{
    /**
     * One consideration, as a plain function.
     *
     * Not a class, not a virtual, not something registered under a name in a
     * map: a function pointer. That is affordable only because a layer has no
     * state -- everything it knows arrives in the snapshot, including its own
     * thresholds -- and it buys three things at once. A policy is shareable
     * between every bot of a class rather than built per bot; adding a layer
     * costs no allocation; and the signature says exactly what a layer may
     * touch, which is a snapshot it cannot write and a sink it can only
     * propose into.
     */
    using Layer = void (*)(Perception const&, IntentSink&);

    /**
     * An ordered list of layers, and the order is the design.
     *
     * The old engine composed behaviour out of named strategies held in a
     * `map<string, Strategy*>` and modulated by `Multiplier` objects that
     * could scale any action's relevance from anywhere. Two consequences, both
     * of which showed up as bugs rather than as design: which strategy
     * supplied an action depended on the alphabetical order of strategy names,
     * and no one place decided anything -- a number was nudged by everyone and
     * settled by no one.
     *
     * Here the list is the precedence. Layers run first-to-last, and a later
     * layer sees nothing of what an earlier one proposed -- it cannot, since
     * the sink is write-only -- so it does not modulate; it proposes its own
     * view at its own urgency and the arbiter settles it. Where two layers
     * agree exactly, the earlier one wins, because `Arbitrate` breaks ties by
     * proposal order.
     *
     * A Policy is immutable once built and holds no per-bot state, so one
     * instance serves every bot of a class and spec for the life of the
     * process.
     */
    struct Policy
    {
        char const*        name = "";
        std::vector<Layer> layers;
    };

    /**
     * The whole decision: run the layers, then choose.
     *
     * A pure function. It takes no `Player*`, reads no global, touches no map,
     * and allocates nothing -- which is what lets it be called for many bots at
     * once, off the world thread, and what lets a test call it with a struct
     * literal and assert on what came back.
     *
     * @param proposals when non-null, receives what was proposed before the
     *        arbiter cut it down. The trace is how a "why did it not heal"
     *        question gets an answer that is not a guess.
     */
    Plan Decide(Policy const& policy, Perception const& perception,
                IntentSink* proposals = nullptr);
}

#endif //MANGOS_BOTSNG_POLICY_H
