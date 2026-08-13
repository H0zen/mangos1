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

#ifndef MANGOS_BOTSNG_LAYER_SURVIVAL_H
#define MANGOS_BOTSNG_LAYER_SURVIVAL_H

#include "../IntentSink.h"
#include "../Percept.h"

namespace bots
{
    /**
     * Staying alive out of the bags: healthstones, potions, food and water.
     *
     * The first layer written, and chosen first because it is the one whose
     * failures are visible from across the room -- a bot that dies with three
     * potions in its bag, or one that stands eating in the middle of a fight.
     *
     * It knows no item ids. Sensing classifies what the bags hold into `Use`
     * categories, so this layer asks for "the best healing potion I can drink"
     * and gets it; the old actions each re-scanned the inventory looking for
     * their own item by matching words in its NAME, at the moment they wanted
     * one, once per action per tick.
     */
    void SurvivalLayer(Perception const& perception, IntentSink& sink);
}

#endif //MANGOS_BOTSNG_LAYER_SURVIVAL_H
