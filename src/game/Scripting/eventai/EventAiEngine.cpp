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

#include "EventAiEngine.h"

#include "Creature.h"
#include "CreatureEventAI.h"
#include "Map.h"

namespace scripting
{
    namespace
    {
        /// What `creature_template.AIName` says to reach this engine.
        char const AI_NAME[] = "EventAI";
    }

    int EventAiEngine::Bid(Context const& ctx, RoleId role, Ref subject)
    {
        if (role != RoleId::CreatureAI)
        {
            return NoBid;
        }

        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return NoBid;
        }

        // GetAnyTypeCreature, not GetCreature: a pet is a creature to this
        // engine and always was. A guardian, a mini-pet and a summoned pet
        // owned by an NPC all reach AI selection with a HIGHGUID_PET guid,
        // and all three are scriptable by EventAI today.
        Creature const* creature =
            ctx.map->GetAnyTypeCreature(ObjectGuid(subject.guid));
        if (!creature)
        {
            return NoBid;
        }

        // Not a totem, ever. EventAI reached a creature through the AI
        // registry, which FactorySelector consulted only after it had already
        // picked TotemAI by NPC flag -- the comment there said so in as many
        // words. That test is the world's, but the refusal is this engine's:
        // EventAI has never driven a totem and does not start now because it
        // moved into the auction, which runs before those flags are read.
        //
        // SD3 deliberately does NOT share this refusal. A script bound by name
        // to a totem's entry has always been able to drive it, because SD3 was
        // asked at the top of that function rather than through the registry.
        // The asymmetry is inherited, not designed.
        if (creature->IsTotem())
        {
            return NoBid;
        }

        // A lookup, which is what a bid has to be -- the answer is one string
        // compare against the creature's template and nothing is built to get
        // it.
        //
        // The creature is resolved rather than the entry being decoded out of
        // the guid, and that is not a detour. GetAIName() reads the entry the
        // creature is CURRENTLY using, which a heroic template or a game-event
        // override replaces after the guid was minted; keying off the guid
        // would bid for the wrong template on exactly those creatures.
        return creature->GetAIName() == AI_NAME ? BidNormal : NoBid;
    }

    CreatureAI* EventAiEngine::MakeCreatureAI(Context const& ctx,
                                              Creature* creature)
    {
        (void)ctx;

        if (!creature)
        {
            return nullptr;
        }

        // Never declines. The bid was decided by the same template field this
        // would re-read, and CreatureEventAI's constructor copes on its own
        // with an entry that has no rows -- it reports the empty event map and
        // drives the creature with no events, which is what an AIName pointing
        // at a table with nothing in it has always produced.
        return new CreatureEventAI(creature);
    }
}
