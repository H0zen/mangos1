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

#include "Utilities/Errors.h"
#include "ScriptHost.h"
#include <vector>
#include "CreatureAISelector.h"
#include "Creature.h"
#include "CreatureAIImpl.h"
#include "NullCreatureAI.h"
#include "Policies/Singleton.h"
#include "MovementGenerator.h"
#include "ScriptMgr.h"
#include "Pet.h"
#include "Log.h"
#include <string>


namespace FactorySelector
{
    /**
     * @brief Selects the most appropriate AI implementation for a creature.
     *
     * @param creature The creature requiring an AI instance.
     * @return The selected AI implementation.
     */
    CreatureAI* selectAI(Creature* creature)
    {
        // Scripting gets the creature first, unless the core owns its control.
        //
        // A player's controlled pet obeys its player and a charmed creature
        // obeys its charmer; both are driven by PetAI and no script may take
        // that over. The test used to be "not a controlled pet" with no regard
        // for whose pet it was, which also kept a pet summoned BY A CREATURE
        // away from the scripts -- Spell::DoSummon makes one of those whenever
        // a non-player casts a summon-pet effect. That was never the intent:
        // the registry test below spells out that guardians, mini-pets and
        // pets controlled by NPCs are meant to be scriptable, and EventAI
        // reached them only because the registry ran after this line. Now that
        // every engine is reached through this one call, the owner has to be
        // checked here too or those pets would lose their scripts.
        Unit const* petOwner = creature->GetOwner();
        bool const ownedByPlayer = petOwner
            && petOwner->GetTypeId() == TYPEID_PLAYER;

        if (!(creature->IsPet() && ((Pet*)creature)->isControlled() && ownedByPlayer)
            && !creature->IsCharmed())
            if (CreatureAI* scriptedAI = scripting::ClaimCreatureAI(creature))
            {
                return scriptedAI;
            }

        CreatureAIRegistry& ai_registry(CreatureAIRepository::Instance());

        const CreatureAICreator* ai_factory = NULL;

        std::string ainame = creature->GetAIName();

        // Select by NPC flags _first_ - otherwise the AI named by the template
        // might be chosen for pets/totems.
        // Explicit check for isControlled() and owner type to allow guardian,
        // mini-pets and pets controlled by NPCs to be scripted.
        //
        // These two are unreachable for a creature the auction above already
        // claimed, which is why EventAI refuses to bid for a totem: it used to
        // be reached only through the AI-name lookup below, so TotemAI got
        // there first, and that has to keep being true now that it bids.
        if ((creature->IsPet() && ((Pet*)creature)->isControlled() && ownedByPlayer)
            || creature->IsCharmed())
        {
            ai_factory = ai_registry.GetRegistryItem("PetAI");
        }
        else if (creature->IsTotem())
        {
            ai_factory = ai_registry.GetRegistryItem("TotemAI");
        }

        // select by script name
        if (!ai_factory && !ainame.empty())
        {
            ai_factory = ai_registry.GetRegistryItem(ainame.c_str());
        }

        if (!ai_factory && creature->IsGuard())
        {
            ai_factory = ai_registry.GetRegistryItem("GuardAI");
        }

        // select by permit check
        if (!ai_factory)
        {
            int best_val = PERMIT_BASE_NO;
            typedef CreatureAIRegistry::RegistryMapType RMT;
            RMT const& l = ai_registry.GetRegisteredItems();
            for (RMT::const_iterator iter = l.begin(); iter != l.end(); ++iter)
            {
                const CreatureAICreator* factory = iter->second;
                const SelectableAI* p = dynamic_cast<const SelectableAI*>(factory);
                MANGOS_ASSERT(p != NULL);
                int val = p->Permit(creature);
                if (val > best_val)
                {
                    best_val = val;
                    ai_factory = p;
                }
            }
        }

        // select NullCreatureAI if not another cases
        ainame = (ai_factory == NULL) ? "NullCreatureAI" : ai_factory->key();

        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature %u used AI is %s.", creature->GetGUIDLow(), ainame.c_str());
        return (ai_factory == NULL ? new NullCreatureAI(creature) : ai_factory->Create(creature));
    }

    /**
     * @brief Selects the default movement generator for a creature.
     *
     * @param creature The creature requiring a movement generator.
     * @return The selected movement generator, or null if none is registered.
     */
    MovementGenerator* selectMovementGenerator(Creature* creature)
    {
        MovementGeneratorRegistry& mv_registry(MovementGeneratorRepository::Instance());
        MANGOS_ASSERT(creature->GetCreatureInfo() != NULL);
        MovementGeneratorCreator const* mv_factory = mv_registry.GetRegistryItem(
                    creature->GetOwnerGuid().IsPlayer() ? FOLLOW_MOTION_TYPE : creature->GetDefaultMovementType());

        /* if ( mv_factory == NULL  )
        {
            int best_val = -1;
            std::vector<std::string> l;
            mv_registry.GetRegisteredItems(l);
            for( std::vector<std::string>::iterator iter = l.begin(); iter != l.end(); ++iter)
            {
            const MovementGeneratorCreator *factory = mv_registry.GetRegistryItem((*iter).c_str());
            const SelectableMovement *p = dynamic_cast<const SelectableMovement *>(factory);
            ASSERT( p != NULL );
            int val = p->Permit(creature);
            if ( val > best_val )
            {
                best_val = val;
                mv_factory = p;
            }
            }
        }*/

        return (mv_factory == NULL ? NULL : mv_factory->Create(creature));
    }
}
