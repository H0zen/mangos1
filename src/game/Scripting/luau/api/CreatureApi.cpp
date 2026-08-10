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

// Creature -- a Unit the server owns rather than a player.
//
// One method changed shape in the port and it matters. Eluna's home position
// was GetRespawnCoord/SetRespawnCoord, four loose floats; here it is Spawn(),
// a Placement, and the comment over that declaration says why: for a creature
// crewing a vessel the spawn pose is a DECK OFFSET and not a map coordinate,
// so handing it to anything that expects world coordinates is wrong. Reading
// x/y/z off the placement keeps the numbers in the frame they belong to, and
// SetHomePosition writes back into the same frame rather than inventing one.

#include "LuaApi.h"
#include "Methods.h"

#include "Creature.h"
#include "CreatureAI.h"
#include "Geometry/Vector3.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Unit.h"
#include "World.h"
#include "WorldPacket.h"

#include "lua.h"
#include "lualib.h"

#include <list>

namespace scripting
{
    namespace api
    {
        namespace
        {
            /// How GetAITarget picks out of the threat list.
            enum SelectAggroTarget
            {
                SELECT_TARGET_RANDOM = 0,
                SELECT_TARGET_TOPAGGRO,
                SELECT_TARGET_BOTTOMAGGRO,
                SELECT_TARGET_NEAREST,
                SELECT_TARGET_FARTHEST
            };

            int IsRegeneratingHealth(Api& a, Creature* c)
            { a.Push(c->IsRegeneratingHealth()); return 1; }

            int IsReputationGainDisabled(Api& a, Creature* c)
            { a.Push(c->IsReputationGainDisabled()); return 1; }

            int IsInEvadeMode(Api& a, Creature* c) { a.Push(c->IsInEvadeMode()); return 1; }
            int IsElite(Api& a, Creature* c)       { a.Push(c->IsElite()); return 1; }
            int IsGuard(Api& a, Creature* c)       { a.Push(c->IsGuard()); return 1; }
            int IsCivilian(Api& a, Creature* c)    { a.Push(c->IsCivilian()); return 1; }
            int IsRacialLeader(Api& a, Creature* c){ a.Push(c->IsRacialLeader()); return 1; }
            int IsWorldBoss(Api& a, Creature* c)   { a.Push(c->IsWorldBoss()); return 1; }
            int CanSwim(Api& a, Creature* c)       { a.Push(c->CanSwim()); return 1; }
            int CanWalk(Api& a, Creature* c)       { a.Push(c->CanWalk()); return 1; }
            int CanFly(Api& a, Creature* c)        { a.Push(c->CanFly()); return 1; }
            int HasLootRecipient(Api& a, Creature* c) { a.Push(c->HasLootRecipient()); return 1; }
            int HasSearchedAssistance(Api& a, Creature* c)
            { a.Push(c->HasSearchedAssistance()); return 1; }

            int CanAggro(Api& a, Creature* c)
            {
                a.Push(!c->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PASSIVE));
                return 1;
            }

            int CanCompleteQuest(Api& a, Creature* c)
            {
                a.Push(c->HasInvolvedQuest(a.Check<uint32>(2)));
                return 1;
            }

            int HasQuest(Api& a, Creature* c)
            {
                a.Push(c->HasQuest(a.Check<uint32>(2)));
                return 1;
            }

            int HasSpell(Api& a, Creature* c)
            {
                a.Push(c->HasSpell(a.Check<uint32>(2)));
                return 1;
            }

            int HasSpellCooldown(Api& a, Creature* c)
            {
                a.Push(c->HasSpellCooldown(a.Check<uint32>(2)));
                return 1;
            }

            int HasCategoryCooldown(Api& a, Creature* c)
            {
                a.Push(c->HasCategoryCooldown(a.Check<uint32>(2)));
                return 1;
            }

            int IsTargetableForAttack(Api& a, Creature* c)
            {
                a.Push(c->IsTargetableForAttack(a.Check<bool>(2, false)));
                return 1;
            }

            int IsTappedBy(Api& a, Creature* c)
            {
                Player* player = a.CheckObj<Player>(2);
                a.Push(player && c->IsTappedBy(player));
                return 1;
            }

            int CanAssistTo(Api& a, Creature* c)
            {
                Unit* who = a.CheckObj<Unit>(2);
                Unit* enemy = a.CheckObj<Unit>(3);
                bool const checkFaction = a.Check<bool>(4, true);

                a.Push(who && enemy && c->CanAssistTo(who, enemy,
                                                      checkFaction));
                return 1;
            }

            int GetRespawnDelay(Api& a, Creature* c)
            { a.Push(c->GetRespawnDelay()); return 1; }

            int GetWanderRadius(Api& a, Creature* c)
            { a.Push(c->GetRespawnRadius()); return 1; }

            int GetCorpseDelay(Api& a, Creature* c)
            { a.Push(c->GetCorpseDelay()); return 1; }

            int GetScriptName(Api& a, Creature* c) { a.Push(c->GetScriptName()); return 1; }
            int GetAIName(Api& a, Creature* c)     { a.Push(c->GetAIName()); return 1; }
            int GetScriptId(Api& a, Creature* c)   { a.Push(c->GetScriptId()); return 1; }

            int GetLootRecipient(Api& a, Creature* c)
            { a.Push(c->GetLootRecipient()); return 1; }

            int GetLootRecipientGroup(Api& a, Creature* c)
            { a.Push(c->GetGroupLootRecipient()); return 1; }

            int GetDefaultMovementType(Api& a, Creature* c)
            { a.Push(uint32(c->GetDefaultMovementType())); return 1; }

            int GetCurrentWaypointId(Api& a, Creature* c)
            { a.Push(c->GetMotionMaster()->getLastReachedWaypoint()); return 1; }

            int GetNPCFlags(Api& a, Creature* c)
            { a.Push(c->GetUInt32Value(UNIT_NPC_FLAGS)); return 1; }

            int GetExtraFlags(Api& a, Creature* c)
            { a.Push(c->GetCreatureInfo()->ExtraFlags); return 1; }

            int GetShieldBlockValue(Api& a, Creature* c)
            { a.Push(c->GetShieldBlockValue()); return 1; }

            int GetDBTableGUIDLow(Api& a, Creature* c)
            { a.Push(c->GetGUIDLow()); return 1; }

            int GetCreatureSpellCooldownDelay(Api& a, Creature* c)
            {
                a.Push(c->GetCreatureSpellCooldownDelay(a.Check<uint32>(2)));
                return 1;
            }

            int GetAttackDistance(Api& a, Creature* c)
            {
                Unit* target = a.CheckObj<Unit>(2);
                a.Push(target ? c->GetAttackDistance(target) : 0.0f);
                return 1;
            }

            int GetAggroRange(Api& a, Creature* c)
            {
                Unit* target = a.CheckObj<Unit>(2);
                if (!target)
                {
                    a.Push(0.0f);
                    return 1;
                }

                float const attack = c->GetAttackDistance(target);
                float const threat =
                    sWorld.getConfig(CONFIG_FLOAT_THREAT_RADIUS);
                a.Push(threat > attack ? threat : attack);
                return 1;
            }

            int GetCreatureFamily(Api& a, Creature* c)
            {
                CreatureInfo const* info =
                    ObjectMgr::GetCreatureTemplate(c->GetEntry());
                a.Push(info ? info->Family : 0);
                return 1;
            }

            /**
             * Where this creature belongs, in the frame it belongs in.
             *
             * For a creature crewing a vessel these are DECK coordinates, not
             * map ones. That is the honest answer and the only one available:
             * see the note over Creature::Spawn().
             */
            int GetHomePosition(Api& a, Creature* c)
            {
                Geometry::Placement const& spawn = c->Spawn();
                a.Push(spawn.X());
                a.Push(spawn.Y());
                a.Push(spawn.Z());
                a.Push(spawn.Facing());
                return 4;
            }

            int SetHomePosition(Api& a, Creature* c)
            {
                float const x = a.Check<float>(2);
                float const y = a.Check<float>(3);
                float const z = a.Check<float>(4);
                float const o = a.Check<float>(5);

                c->SetSpawn(Geometry::Vector3(x, y, z), o);
                return 0;
            }

            int GetAITargets(Api& a, Creature* c)
            {
                ThreatList const& threats =
                    c->GetThreatManager().getThreatList();

                lua_State* L = a.L;
                lua_newtable(L);

                int index = 1;
                for (HostileReference* reference : threats)
                {
                    if (Unit* target = reference->getTarget())
                    {
                        a.Push(target);
                        lua_rawseti(L, -2, index++);
                    }
                }
                return 1;
            }

            int GetAITargetsCount(Api& a, Creature* c)
            {
                a.Push(uint32(c->GetThreatManager().getThreatList().size()));
                return 1;
            }

            int GetAITarget(Api& a, Creature* c)
            {
                uint32 const targetType = a.Check<uint32>(2);
                bool const playerOnly = a.Check<bool>(3, false);
                uint32 const position = a.Check<uint32>(4, 0);
                float const dist = a.Check<float>(5, 0.0f);
                int32 const aura = a.Check<int32>(6, 0);

                std::list<Unit*> candidates;
                for (HostileReference* reference :
                     c->GetThreatManager().getThreatList())
                {
                    Unit* target = reference->getTarget();
                    if (!target)
                    {
                        continue;
                    }
                    if (playerOnly && target->GetTypeId() != TYPEID_PLAYER)
                    {
                        continue;
                    }
                    if (aura > 0 && !target->HasAura(uint32(aura)))
                    {
                        continue;
                    }
                    if (aura < 0 && target->HasAura(uint32(-aura)))
                    {
                        continue;
                    }

                    // A positive distance means "no further than", a negative
                    // one "no closer than" -- Eluna's convention, kept.
                    if (dist > 0.0f &&
                        !c->Where().WithinDist(target->Where(), dist))
                    {
                        continue;
                    }
                    if (dist < 0.0f &&
                        c->Where().WithinDist(target->Where(), -dist))
                    {
                        continue;
                    }

                    candidates.push_back(target);
                }

                if (candidates.empty() || position >= candidates.size())
                {
                    a.Push();
                    return 1;
                }

                if (targetType == SELECT_TARGET_NEAREST ||
                    targetType == SELECT_TARGET_FARTHEST)
                {
                    Geometry::Placement const& from = c->Where();
                    candidates.sort([&from](Unit* lhs, Unit* rhs)
                    {
                        return from.IsNearer(lhs->Where(), rhs->Where());
                    });
                }

                switch (targetType)
                {
                    case SELECT_TARGET_NEAREST:
                    case SELECT_TARGET_TOPAGGRO:
                    {
                        auto itr = candidates.begin();
                        std::advance(itr, position);
                        a.Push(*itr);
                        break;
                    }
                    case SELECT_TARGET_FARTHEST:
                    case SELECT_TARGET_BOTTOMAGGRO:
                    {
                        auto ritr = candidates.rbegin();
                        std::advance(ritr, position);
                        a.Push(*ritr);
                        break;
                    }
                    case SELECT_TARGET_RANDOM:
                    {
                        auto itr = candidates.begin();
                        std::advance(itr, position
                                              ? urand(0, position)
                                              : urand(0, uint32(candidates.size()) - 1));
                        a.Push(*itr);
                        break;
                    }
                    default:
                        luaL_argerror(a.L, 2, "SelectAggroTarget expected");
                        break;
                }
                return 1;
            }

            // ---- changing -------------------------------------------------

            int SetNPCFlags(Api& a, Creature* c)
            {
                c->SetUInt32Value(UNIT_NPC_FLAGS, a.Check<uint32>(2));
                return 0;
            }

            int SetDisableGravity(Api& a, Creature* c)
            {
                c->SetLevitate(a.Check<bool>(2, true));
                return 0;
            }

            int SetDeathState(Api& a, Creature* c)
            {
                c->SetDeathState(DeathState(a.Check<int32>(2)));
                return 0;
            }

            int SetWalk(Api& a, Creature* c)
            {
                c->SetWalk(a.Check<bool>(2, true));
                return 0;
            }

            int SetEquipmentSlots(Api& a, Creature* c)
            {
                c->SetVirtualItem(VIRTUAL_ITEM_SLOT_0, a.Check<uint32>(2));
                c->SetVirtualItem(VIRTUAL_ITEM_SLOT_1, a.Check<uint32>(3));
                c->SetVirtualItem(VIRTUAL_ITEM_SLOT_2, a.Check<uint32>(4));
                return 0;
            }

            int SetAggroEnabled(Api& a, Creature* c)
            {
                if (a.Check<bool>(2, true))
                {
                    c->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PASSIVE);
                }
                else
                {
                    c->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PASSIVE);
                }
                return 0;
            }

            int SetDisableReputationGain(Api& a, Creature* c)
            {
                c->SetDisableReputationGain(a.Check<bool>(2, true));
                return 0;
            }

            int SetInCombatWithZone(Api& a, Creature* c)
            {
                (void)a;
                c->SetInCombatWithZone();
                return 0;
            }

            int SetWanderRadius(Api& a, Creature* c)
            {
                c->SetRespawnRadius(a.Check<float>(2));
                return 0;
            }

            int SetRespawnDelay(Api& a, Creature* c)
            {
                c->SetRespawnDelay(a.Check<uint32>(2));
                return 0;
            }

            int SetDefaultMovementType(Api& a, Creature* c)
            {
                c->SetDefaultMovementType(
                    MovementGeneratorType(a.Check<int32>(2)));
                return 0;
            }

            int SetNoSearchAssistance(Api& a, Creature* c)
            {
                c->SetNoSearchAssistance(a.Check<bool>(2, true));
                return 0;
            }

            int SetNoCallAssistance(Api& a, Creature* c)
            {
                c->SetNoCallAssistance(a.Check<bool>(2, true));
                return 0;
            }

            // ---- doing ----------------------------------------------------

            int DespawnOrUnsummon(Api& a, Creature* c)
            {
                c->ForcedDespawn(a.Check<uint32>(2, 0));
                return 0;
            }

            int Respawn(Api& a, Creature* c)
            {
                (void)a;
                c->Respawn();
                return 0;
            }

            int RemoveCorpse(Api& a, Creature* c)
            {
                (void)a;
                c->RemoveCorpse();
                return 0;
            }

            int MoveWaypoint(Api& a, Creature* c)
            {
                (void)a;
                c->GetMotionMaster()->MoveWaypoint();
                return 0;
            }

            int CallAssistance(Api& a, Creature* c)
            {
                (void)a;
                c->CallAssistance();
                return 0;
            }

            int CallForHelp(Api& a, Creature* c)
            {
                c->CallForHelp(a.Check<float>(2));
                return 0;
            }

            int FleeToGetAssistance(Api& a, Creature* c)
            {
                (void)a;
                c->DoFleeToGetAssistance();
                return 0;
            }

            int AttackStart(Api& a, Creature* c)
            {
                Unit* target = a.CheckObj<Unit>(2);
                if (target && c->AI())
                {
                    c->AI()->AttackStart(target);
                }
                return 0;
            }

            int SaveToDB(Api& a, Creature* c)
            {
                (void)a;
                c->SaveToDB();
                return 0;
            }

            int SelectVictim(Api& a, Creature* c)
            {
                a.Push(c->SelectHostileTarget());
                return 1;
            }

            int UpdateEntry(Api& a, Creature* c)
            {
                uint32 const entry = a.Check<uint32>(2);
                uint32 const dataGuidLow = a.Check<uint32>(3, 0);

                c->UpdateEntry(entry, ALLIANCE,
                               dataGuidLow
                                   ? sObjectMgr.GetCreatureData(dataGuidLow)
                                   : nullptr);
                return 0;
            }
        }

        MethodEntry const* CreatureMethods(std::size_t& count)
        {
#define M(name)                                                               \
            { #name, [](Api& a, void* self) -> int                            \
                { return name(a, static_cast<Creature*>(self)); } }

            static MethodEntry const table[] =
            {
                M(IsRegeneratingHealth), M(IsReputationGainDisabled),
                M(IsInEvadeMode), M(IsElite), M(IsGuard), M(IsCivilian),
                M(IsRacialLeader), M(IsWorldBoss), M(CanSwim), M(CanWalk),
                M(CanFly), M(CanAggro), M(HasLootRecipient),
                M(HasSearchedAssistance), M(CanCompleteQuest), M(HasQuest),
                M(HasSpell), M(HasSpellCooldown), M(HasCategoryCooldown),
                M(IsTargetableForAttack), M(IsTappedBy), M(CanAssistTo),

                M(GetRespawnDelay), M(GetWanderRadius), M(GetCorpseDelay),
                M(GetScriptName), M(GetAIName), M(GetScriptId),
                M(GetLootRecipient), M(GetLootRecipientGroup),
                M(GetDefaultMovementType), M(GetCurrentWaypointId),
                M(GetNPCFlags), M(GetExtraFlags), M(GetShieldBlockValue),
                M(GetDBTableGUIDLow), M(GetCreatureSpellCooldownDelay),
                M(GetAttackDistance), M(GetAggroRange), M(GetCreatureFamily),
                M(GetHomePosition), M(SetHomePosition),
                M(GetAITarget), M(GetAITargets), M(GetAITargetsCount),

                M(SetNPCFlags), M(SetDisableGravity), M(SetDeathState),
                M(SetWalk), M(SetEquipmentSlots), M(SetAggroEnabled),
                M(SetDisableReputationGain), M(SetInCombatWithZone),
                M(SetWanderRadius), M(SetRespawnDelay),
                M(SetDefaultMovementType), M(SetNoSearchAssistance),
                M(SetNoCallAssistance),

                M(DespawnOrUnsummon), M(Respawn), M(RemoveCorpse),
                M(MoveWaypoint), M(CallAssistance), M(CallForHelp),
                M(FleeToGetAssistance), M(AttackStart), M(SaveToDB),
                M(SelectVictim), M(UpdateEntry),

                // Eluna sent the hover state as a raw movement packet. That
                // opcode pair does not exist in 2.4.3 -- hovering is an aura
                // here -- so the honest answer is that this is not a thing a
                // script sets directly.
                { "SetHover", nullptr },
            };

#undef M

            count = sizeof(table) / sizeof(table[0]);
            return table;
        }
    }
}
