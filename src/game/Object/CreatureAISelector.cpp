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
#include <vector>
#include "CreatureAISelector.h"
#include "Creature.h"
#include "CreatureAIImpl.h"
#include "NullCreatureAI.h"
#include "Policies/Singleton.h"
#include "MovementGenerator.h"
#include "ScriptHost.h"
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
        // Allow scripting AI for normal creatures and not controlled pets (guardians and mini-pets)
        if ((!creature->IsPet() || !((Pet*)creature)->isControlled()) && !creature->IsCharmed())
            if (CreatureAI* scriptedAI = sScriptMgr.GetCreatureAI(creature))
            {
                return scriptedAI;
            }

        CreatureAIRegistry& ai_registry(CreatureAIRepository::Instance());

        const CreatureAICreator* ai_factory = NULL;

        std::string ainame = creature->GetAIName();

        // Select by NPC flags _first_: a creature whose control the core owns
        // must be driven by the AI that implements that control, whatever a
        // template or a script would rather have. This is the test the old
        // comment called "otherwise EventAI might be choosen for pets/totems",
        // and it still is -- the engine auction below sits where EventAI used
        // to be reached by name, so these two decisions keep beating it.
        //
        // The explicit isControlled() and owner-type check is load-bearing and
        // stays exactly as it is: it is what lets guardians, mini-pets and
        // pets controlled by NPCs fall past PetAI and be scripted. A pet
        // summoned by a creature is isControlled() with a non-player owner --
        // Spell::DoSummon makes one whenever a non-player casts a summon-pet
        // effect -- so it never reaches the scripted call above and the
        // auction below is its only way to a script.
        Unit* owner = NULL;
        if ((creature->IsPet() && ((Pet*)creature)->isControlled() &&
             ((owner = creature->GetOwner()) && owner->GetTypeId() == TYPEID_PLAYER)) || creature->IsCharmed())
        {
            ai_factory = ai_registry.GetRegistryItem("PetAI");
        }
        else if (creature->IsTotem())
        {
            ai_factory = ai_registry.GetRegistryItem("TotemAI");
        }

        // Let the scripting engines bid for the creature.
        //
        // This is where EventAI was picked up when it was a registry entry
        // named "EventAI", and the position is the whole point: after the NPC
        // flags above have had their say, and ahead of selection by AI name.
        // Putting the auction at the top of this function instead would have
        // moved EventAI ahead of SD3, which is consulted through
        // sScriptMgr.GetCreatureAI(), and taken creatures away from it.
        //
        // SD3 is not an engine yet, so it is still asked separately and still
        // asked first. When it becomes one it bids here like everything else,
        // strongly, because its binding is to a named script rather than to a
        // template field -- and this call is then the only one left.
        if (!ai_factory)
        {
            if (CreatureAI* claimed = scripting::ClaimCreatureAI(creature))
            {
                // The line below never runs for a claimed creature, and one
                // used to say "used AI is EventAI" for every creature this
                // branch now takes. Which engine won is deliberately not
                // named -- the selector must not learn engine names to log
                // them -- and `.npc aiinfo` still prints the AI class.
                DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS,
                                 "Creature %u AI claimed by a scripting engine.",
                                 creature->GetGUIDLow());
                return claimed;
            }
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
