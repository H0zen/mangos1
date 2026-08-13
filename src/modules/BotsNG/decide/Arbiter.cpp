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

#include "Arbiter.h"

#include <algorithm>
#include <numeric>

namespace bots
{
    namespace
    {
        /// Whether the body can be given orders at all. Not a policy question
        /// -- a stunned character does not decline to move, it cannot -- which
        /// is why it lives here and not in a layer.
        bool CanAct(Self const& self)
        {
            return !self.dead && !self.stunned;
        }

        /**
         * Whether @a intent may start given what is already spent.
         *
         * The one judgement call in this function is the cast in flight. A bot
         * already casting refuses another cast, EXCEPT at survival urgency:
         * interrupting your own Greater Heal to swallow a healthstone at 8%
         * health is right, and interrupting it for the next tick of a rotation
         * is how a caster spends a whole fight starting spells it never
         * finishes. The threshold is the line between those two, and it is
         * written down here rather than left to each layer to remember.
         */
        bool Fits(Self const& self, Intent const& intent, Channel channel,
                  std::uint32_t powerSpent)
        {
            if (intent.cost.gcdMs > 0 && self.gcdLeftMs > 0)
            {
                return false;
            }

            if (channel == Channel::Action && self.castingSpell != 0 &&
                intent.score < ScoreSurvival)
            {
                return false;
            }

            if (intent.cost.power > 0 &&
                intent.cost.power + powerSpent > self.power)
            {
                return false;
            }

            return true;
        }
    }

    Plan Arbitrate(Self const& self, IntentSink const& proposals)
    {
        Plan plan;

        if (!CanAct(self))
        {
            // Everything proposed is rejected rather than ignored, so a trace
            // shows a stunned bot as "wanted five things, could do none"
            // rather than as a bot that thought of nothing.
            plan.rejected = proposals.Count();
            return plan;
        }

        // Sort indices, not intents: an Intent holds a variant, and ordering
        // sixteen of those by value copies far more than ordering sixteen
        // integers does.
        std::array<std::size_t, MaxProposals> order{};
        const std::size_t total = proposals.Count();
        std::iota(order.begin(), order.begin() + total, std::size_t(0));

        std::stable_sort(order.begin(), order.begin() + total,
            [&proposals](std::size_t lhs, std::size_t rhs)
            {
                return proposals.At(lhs).score > proposals.At(rhs).score;
            });

        bool taken[4] = {false, false, false, false};
        std::uint32_t powerSpent = 0;

        for (std::size_t i = 0; i < total; ++i)
        {
            Intent const& intent = proposals.At(order[i]);
            const Channel channel = ChannelOf(intent.verb);

            if (channel == Channel::None)
            {
                // Idle competes for nothing and is never "chosen"; it exists so
                // that a layer which looked and declined leaves a mark.
                continue;
            }

            const std::size_t slot = static_cast<std::size_t>(channel);
            if (taken[slot] || !Fits(self, intent, channel, powerSpent))
            {
                ++plan.rejected;
                continue;
            }

            taken[slot] = true;
            powerSpent += intent.cost.power;
            plan.intents[plan.count++] = intent;
        }

        return plan;
    }
}
