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

#include "GameObjectUseHandlers.h"

#include "BattleGround/BattleGround.h"
#include "GameObject.h"
#include "Log.h"
#include "MapPersistentStateMgr.h"
#include "Player.h"
#include "PlayerRegistry.h"
#include "ScriptTypes.h"
#include "WorldHooks.h"

namespace GameObjectUse
{
    namespace Types
    {
        namespace
        {
            /// The user as a player, or null. Nine of the seventeen types do
            /// nothing at all for anything else.
            Player* PlayerOf(Use const& use)
            {
                if (!use.user || use.user->GetTypeId() != TYPEID_PLAYER)
                {
                    return nullptr;
                }

                return static_cast<Player*>(use.user);
            }

            /// The object's own activation script, for the types that have
            /// one. Skipped when a script already claimed the use: a claim
            /// means the behaviour was produced, and running the activation
            /// on top of it produces it twice.
            void Activate(Use const& use, Outcome const& outcome)
            {
                if (use.claimed)
                {
                    return;
                }

                scripting::Notify(use.object->GetMap(),
                    scripting::GameobjectActivate{
                        scripting::RefOf(outcome.caster),
                        scripting::RefOf(use.object) });
            }
        }

        void Door(Use const& use, Outcome& outcome)
        {
            use.object->UseDoorOrButton();
            Activate(use, outcome);
        }

        void Button(Use const& use, Outcome& outcome)
        {
            use.object->UseDoorOrButton();
            use.object->TriggerLinkedGameObject(use.user);
            Activate(use, outcome);
        }

        void QuestGiver(Use const& use, Outcome& /*outcome*/)
        {
            Player* player = PlayerOf(use);
            if (!player)
            {
                return;
            }

            if (!scripting::GossipHello(player, use.object))
            {
                player->PrepareGossipMenu(
                    use.object,
                    use.object->GetGOInfo()->questgiver.gossipID);
                player->SendPreparedGossip(use.object);
            }
        }

        void Chest(Use const& use, Outcome& /*outcome*/)
        {
            if (!PlayerOf(use))
            {
                return;
            }

            use.object->TriggerLinkedGameObject(use.user);

            // TODO: possible must be moved to loot release (in different from
            // linked triggering)
            if (use.object->GetGOInfo()->chest.eventId)
            {
                DEBUG_LOG("Chest ScriptStart id %u for %s (opened by %s)",
                          use.object->GetGOInfo()->chest.eventId,
                          use.object->GetGuidStr().c_str(),
                          use.user->GetGuidStr().c_str());

                StartEvents_Event(use.object->GetMap(),
                                  use.object->GetGOInfo()->chest.eventId,
                                  use.user, use.object);
            }
        }

        void Generic(Use const& use, Outcome& /*outcome*/)
        {
            if (use.claimed)
            {
                return;
            }

            // No known way to exclude some -- the only other approach is to
            // select despawnable objects by entry.
            use.object->SetLootState(GO_JUST_DEACTIVATED);
        }

        void SpellFocus(Use const& use, Outcome& /*outcome*/)
        {
            use.object->TriggerLinkedGameObject(use.user);

            // Some may be activated in addition; the condition for it is not
            // known (entry 181616 is the example).
        }

        void Camera(Use const& use, Outcome& /*outcome*/)
        {
            GameObjectInfo const* info = use.object->GetGOInfo();
            if (!info)
            {
                return;
            }

            Player* player = PlayerOf(use);
            if (!player)
            {
                return;
            }

            if (info->camera.cinematicId)
            {
                player->SendCinematicStart(info->camera.cinematicId);
            }

            if (info->camera.eventID)
            {
                StartEvents_Event(use.object->GetMap(), info->camera.eventID,
                                  player, use.object);
            }
        }

        void SpellCaster(Use const& use, Outcome& outcome)
        {
            use.object->SetUInt32Value(GAMEOBJECT_FLAGS, GO_FLAG_LOCKED);

            GameObjectInfo const* info = use.object->GetGOInfo();
            if (!info)
            {
                return;
            }

            if (info->spellcaster.partyOnly)
            {
                Unit* owner = use.object->GetOwner();
                if (!owner || owner->GetTypeId() != TYPEID_PLAYER)
                {
                    return;
                }

                Player* player = PlayerOf(use);
                if (!player ||
                    !player->IsInSameRaidWith(static_cast<Player*>(owner)))
                {
                    return;
                }
            }

            outcome.spellId = info->spellcaster.spellId;

            use.object->AddUse();
        }

        void MeetingStone(Use const& use, Outcome& outcome)
        {
            GameObjectInfo const* info = use.object->GetGOInfo();

            Player* player = PlayerOf(use);
            if (!player || !info)
            {
                return;
            }

            Player* target = sPlayerRegistry.Find(player->GetSelectionGuid());

            // Only somebody else in the same group.
            if (!target || target == player ||
                !target->IsInSameGroupWith(player))
            {
                return;
            }

            // Both ends have to be inside the stone's level range.
            uint8 level = player->getLevel();
            if (level < info->meetingstone.minLevel ||
                level > info->meetingstone.maxLevel)
            {
                return;
            }

            level = target->getLevel();
            if (level < info->meetingstone.minLevel ||
                level > info->meetingstone.maxLevel)
            {
                return;
            }

            outcome.spellId = 23598;
        }

        void FlagStand(Use const& use, Outcome& /*outcome*/)
        {
            Player* player = PlayerOf(use);
            if (!player || !player->CanUseBattleGroundObject())
            {
                return;
            }

            BattleGround* bg = player->GetBattleGround();
            if (!bg)
            {
                return;
            }

            // The flag is despawned by the event rather than deleted here.
            bg->EventPlayerClickedOnFlag(player, use.object);
        }

        void FishingHole(Use const& use, Outcome& /*outcome*/)
        {
            Player* player = PlayerOf(use);
            if (!player)
            {
                return;
            }

            player->SendLoot(use.object->GetObjectGuid(), LOOT_FISHINGHOLE);
        }
    }
}
