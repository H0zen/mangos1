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

#include "Survival.h"

#include <algorithm>

namespace bots
{
    namespace
    {
        /**
         * How much more it matters, the further past the threshold we are.
         *
         * Capped at 49, and the cap is the point: the score bands are two
         * hundred apart, so no amount of urgency inside one band can ever
         * reach the next. A rule cannot promote itself out of its own class by
         * being extreme, which is exactly what the old relevance arithmetic
         * allowed -- alternatives re-pushed at +0.03 eventually outranked
         * things that were categorically more important.
         */
        Score Urgency(Score base, std::uint8_t pct, std::uint8_t threshold)
        {
            if (pct >= threshold)
            {
                return base;
            }
            const int deficit = static_cast<int>(threshold) -
                                static_cast<int>(pct);
            return base + std::min(deficit, 49);
        }

        bool UsesMana(Self const& self)
        {
            return self.powerKind == PowerKind::Mana;
        }
    }

    void SurvivalLayer(Perception const& perception, IntentSink& sink)
    {
        Self const& self = perception.self;
        Tuning const& tune = perception.tune;

        if (self.dead)
        {
            return;
        }

        if (self.inCombat)
        {
            // A healthstone before a potion, always, and not because it heals
            // more. They are on separate cooldowns, and the stone is the one
            // that cannot be bought back mid-fight -- spending the potion first
            // means the second emergency has nothing left to answer it.
            if (self.healthPct <= tune.healthstoneAt)
            {
                if (Carried const* stone = BestCarried(self, Use::Healthstone))
                {
                    sink.Propose(ProposeUse(stone->itemEntry, self.guid,
                        Urgency(ScoreSurvival, self.healthPct,
                                tune.healthstoneAt),
                        "healthstone, low health in combat"));
                }
            }

            if (self.healthPct <= tune.healPotionAt)
            {
                if (Carried const* potion = BestCarried(self, Use::Heal))
                {
                    sink.Propose(ProposeUse(potion->itemEntry, self.guid,
                        Urgency(ScoreSurvival - 20, self.healthPct,
                                tune.healPotionAt),
                        "healing potion, low health in combat"));
                }
            }

            if (UsesMana(self) && self.powerPct <= tune.manaPotionAt)
            {
                if (Carried const* potion = BestCarried(self, Use::Mana))
                {
                    sink.Propose(ProposeUse(potion->itemEntry, self.guid,
                        Urgency(ScoreEmergency, self.powerPct,
                                tune.manaPotionAt),
                        "mana potion, empty in combat"));
                }
            }

            return;
        }

        // Out of combat. Eating and drinking are both sitting still, and both
        // take the action channel, so the arbiter lets exactly one of them
        // through per tick and the other follows on the next one. That falls
        // out of the channel model rather than being arranged here, and it is
        // the behaviour wanted: health first, then water.
        if (self.mounted)
        {
            return;
        }

        if (!self.eating && self.healthPct <= tune.eatAt)
        {
            if (Carried const* food = BestCarried(self, Use::Food))
            {
                sink.Propose(ProposeUse(food->itemEntry, self.guid,
                    Urgency(ScoreUpkeep, self.healthPct, tune.eatAt),
                    "eat, hurt and out of combat"));
            }
        }

        if (UsesMana(self) && !self.drinking && self.powerPct <= tune.drinkAt)
        {
            if (Carried const* drink = BestCarried(self, Use::Drink))
            {
                sink.Propose(ProposeUse(drink->itemEntry, self.guid,
                    Urgency(ScoreUpkeep - 10, self.powerPct, tune.drinkAt),
                    "drink, low mana and out of combat"));
            }
        }
    }
}
