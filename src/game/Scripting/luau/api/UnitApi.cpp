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

// Unit -- anything with health that can fight: a Player, a Creature, a pet.
//
// The largest table here, and the one that ported most directly: nearly every
// call Eluna made on a Unit exists in this core under the same name, which is
// what a shared lineage buys. What did not port is listed at the bottom of the
// table with a null body, and each of those is a 2.4.3 fact rather than an
// omission -- vehicles and the critter slot arrive in Wrath, and a script
// asking for them here is asking for something the client would not honour.

#include "LuaApi.h"
#include "Methods.h"

#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GridDefines.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "Object.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldPacket.h"

#include "lua.h"
#include "lualib.h"

#include <list>
#include <string>

namespace scripting
{
    namespace api
    {
        namespace
        {
            /**
             * Which power a call means.
             *
             * Absent means "whatever this unit runs on", which is what makes
             * GetPower() work without the caller knowing whether it is
             * talking to a mage or a rogue.
             */
            Powers PowerAt(Api& a, Unit* unit, int narg)
            {
                if (a.IsNoneOrNil(narg))
                {
                    return unit->GetPowerType();
                }

                int32 const type = a.Check<int32>(narg);
                if (type < 0 || type >= int32(MAX_POWERS))
                {
                    luaL_argerror(a.L, narg, "valid Powers expected");
                }
                return Powers(type);
            }

            /// See WorldObjectApi's SearchRange: a grid walk costs what it is
            /// given, and what it is given came from a script.
            float SearchRange(Api& a, int narg)
            {
                float const range = a.Check<float>(narg, SIZE_OF_GRIDS);
                if (range < 0.0f || range > SIZE_OF_GRIDS)
                {
                    luaL_argerror(a.L, narg, "a search range from 0 to one "
                                             "grid is expected here");
                }
                return range;
            }

            template <class Check>
            void PushUnitsAround(Api& a, Unit* unit, float range)
            {
                std::list<Unit*> found;
                Check check(unit, range);
                MaNGOS::UnitListSearcher<Check> searcher(found, check);
                Cell::VisitAllObjects(unit, searcher, range);

                lua_State* L = a.L;
                lua_newtable(L);

                int index = 1;
                for (Unit* candidate : found)
                {
                    if (candidate == unit)
                    {
                        continue;
                    }
                    a.Push(candidate);
                    lua_rawseti(L, -2, index++);
                }
            }

            // ---- asking ---------------------------------------------------

            int IsAlive(Api& a, Unit* u)        { a.Push(u->IsAlive()); return 1; }
            int IsDead(Api& a, Unit* u)         { a.Push(u->IsDead()); return 1; }
            int IsDying(Api& a, Unit* u)        { a.Push(u->IsDying()); return 1; }
            int IsInCombat(Api& a, Unit* u)     { a.Push(u->IsInCombat()); return 1; }
            int IsMounted(Api& a, Unit* u)      { a.Push(u->IsMounted()); return 1; }
            int IsStandState(Api& a, Unit* u)   { a.Push(u->IsStandState()); return 1; }
            int IsFullHealth(Api& a, Unit* u)   { a.Push(u->IsFullHealth()); return 1; }
            int IsStopped(Api& a, Unit* u)      { a.Push(u->IsStopped()); return 1; }
            int IsInWater(Api& a, Unit* u)      { a.Push(u->IsInWater()); return 1; }
            int IsUnderWater(Api& a, Unit* u)   { a.Push(u->IsUnderWater()); return 1; }
            int IsCharmed(Api& a, Unit* u)      { a.Push(u->IsCharmed()); return 1; }
            int IsPvPFlagged(Api& a, Unit* u)   { a.Push(u->IsPvP()); return 1; }
            int IsBanker(Api& a, Unit* u)       { a.Push(u->IsBanker()); return 1; }
            int IsBattleMaster(Api& a, Unit* u) { a.Push(u->IsBattleMaster()); return 1; }
            int IsArmorer(Api& a, Unit* u)      { a.Push(u->IsArmorer()); return 1; }
            int IsVendor(Api& a, Unit* u)       { a.Push(u->IsVendor()); return 1; }
            int IsAuctioneer(Api& a, Unit* u)   { a.Push(u->isAuctioner()); return 1; }
            int IsGuildMaster(Api& a, Unit* u)  { a.Push(u->IsGuildMaster()); return 1; }
            int IsInnkeeper(Api& a, Unit* u)    { a.Push(u->IsInnkeeper()); return 1; }
            int IsTrainer(Api& a, Unit* u)      { a.Push(u->IsTrainer()); return 1; }
            int IsGossip(Api& a, Unit* u)       { a.Push(u->IsGossip()); return 1; }
            int IsTaxi(Api& a, Unit* u)         { a.Push(u->IsTaxi()); return 1; }
            int IsSpiritHealer(Api& a, Unit* u) { a.Push(u->IsSpiritHealer()); return 1; }
            int IsSpiritGuide(Api& a, Unit* u)  { a.Push(u->IsSpiritGuide()); return 1; }
            int IsSpiritService(Api& a, Unit* u){ a.Push(u->IsSpiritService()); return 1; }
            int IsQuestGiver(Api& a, Unit* u)   { a.Push(u->IsQuestGiver()); return 1; }
            int IsTabardDesigner(Api& a, Unit* u) { a.Push(u->IsTabardDesigner()); return 1; }
            int IsServiceProvider(Api& a, Unit* u) { a.Push(u->IsServiceProvider()); return 1; }
            int IsAttackingPlayer(Api& a, Unit* u) { a.Push(u->isAttackingPlayer()); return 1; }
            int CanModifyStats(Api& a, Unit* u)  { a.Push(u->CanModifyStats()); return 1; }

            int IsRooted(Api& a, Unit* u)
            {
                a.Push(u->IsInRoots() || u->IsRooted());
                return 1;
            }

            int IsCasting(Api& a, Unit* u)
            {
                a.Push(u->IsNonMeleeSpellCasted(false));
                return 1;
            }

            int IsInAccessiblePlaceFor(Api& a, Unit* u)
            {
                Creature* creature = a.CheckObj<Creature>(2);
                a.Push(creature && u->isInAccessablePlaceFor(creature));
                return 1;
            }

            int HasAura(Api& a, Unit* u)
            {
                a.Push(u->HasAura(a.Check<uint32>(2)));
                return 1;
            }

            int HasUnitState(Api& a, Unit* u)
            {
                a.Push(u->hasUnitState(a.Check<uint32>(2)));
                return 1;
            }

            int HealthBelowPct(Api& a, Unit* u)
            {
                a.Push(u->HealthBelowPct(a.Check<int32>(2)));
                return 1;
            }

            int HealthAbovePct(Api& a, Unit* u)
            {
                a.Push(u->HealthAbovePct(a.Check<int32>(2)));
                return 1;
            }

            // ---- reading --------------------------------------------------

            int GetLevel(Api& a, Unit* u)       { a.Push(u->getLevel()); return 1; }
            int GetHealth(Api& a, Unit* u)      { a.Push(u->GetHealth()); return 1; }
            int GetMaxHealth(Api& a, Unit* u)   { a.Push(u->GetMaxHealth()); return 1; }
            int GetHealthPct(Api& a, Unit* u)   { a.Push(u->GetHealthPercent()); return 1; }
            int GetGender(Api& a, Unit* u)      { a.Push(uint8(u->getGender())); return 1; }
            int GetRace(Api& a, Unit* u)        { a.Push(uint8(u->getRace())); return 1; }
            int GetClass(Api& a, Unit* u)       { a.Push(uint8(u->getClass())); return 1; }
            int GetRaceMask(Api& a, Unit* u)    { a.Push(u->getRaceMask()); return 1; }
            int GetClassMask(Api& a, Unit* u)   { a.Push(u->getClassMask()); return 1; }
            int GetFaction(Api& a, Unit* u)     { a.Push(u->getFaction()); return 1; }
            int GetCreatureType(Api& a, Unit* u){ a.Push(uint32(u->GetCreatureType())); return 1; }
            int GetDisplayId(Api& a, Unit* u)   { a.Push(u->GetDisplayId()); return 1; }
            int GetNativeDisplayId(Api& a, Unit* u) { a.Push(u->GetNativeDisplayId()); return 1; }
            int GetMountId(Api& a, Unit* u)     { a.Push(u->GetMountID()); return 1; }
            int GetStandState(Api& a, Unit* u)  { a.Push(uint8(u->getStandState())); return 1; }
            int GetPowerType(Api& a, Unit* u)   { a.Push(uint8(u->GetPowerType())); return 1; }
            int GetOwner(Api& a, Unit* u)       { a.Push(u->GetOwner()); return 1; }
            int GetVictim(Api& a, Unit* u)      { a.Push(u->getVictim()); return 1; }

            int GetOwnerGUID(Api& a, Unit* u)   { a.Push(u->GetOwnerGuid()); return 1; }
            int GetCreatorGUID(Api& a, Unit* u) { a.Push(u->GetCreatorGuid()); return 1; }
            int GetCharmerGUID(Api& a, Unit* u) { a.Push(u->GetCharmerGuid()); return 1; }
            int GetCharmGUID(Api& a, Unit* u)   { a.Push(u->GetCharmGuid()); return 1; }
            int GetPetGUID(Api& a, Unit* u)     { a.Push(u->GetPetGuid()); return 1; }

            int GetControllerGUID(Api& a, Unit* u)
            {
                a.Push(u->GetCharmerOrOwnerGuid());
                return 1;
            }

            int GetControllerGUIDS(Api& a, Unit* u)
            {
                a.Push(u->GetCharmerOrOwnerOrOwnGuid());
                return 1;
            }

            int GetPower(Api& a, Unit* u)
            {
                a.Push(u->GetPower(PowerAt(a, u, 2)));
                return 1;
            }

            int GetMaxPower(Api& a, Unit* u)
            {
                a.Push(u->GetMaxPower(PowerAt(a, u, 2)));
                return 1;
            }

            int GetPowerPct(Api& a, Unit* u)
            {
                Powers const power = PowerAt(a, u, 2);
                uint32 const max = u->GetMaxPower(power);

                // Eluna divided without asking. A unit with no maximum in a
                // power it does not use gives 0/0, and the NaN that produces
                // travels silently through every comparison a script makes
                // with it.
                a.Push(max ? (float(u->GetPower(power)) / float(max)) * 100.0f
                           : 0.0f);
                return 1;
            }

            int GetStat(Api& a, Unit* u)
            {
                uint32 const stat = a.Check<uint32>(2);
                if (stat >= MAX_STATS)
                {
                    luaL_argerror(a.L, 2, "valid Stats expected");
                }
                a.Push(u->GetStat(Stats(stat)));
                return 1;
            }

            /**
             * Spell power, which only a player has a field for.
             *
             * PLAYER_FIELD_MOD_DAMAGE_DONE_POS sits past UNIT_END, and a
             * Creature's block ends AT UNIT_END -- so reading it off anything
             * but a player tripped the MANGOS_ASSERT inside GetUInt32Value and
             * took the process with it. The method is on Unit because that is
             * where Eluna put it and where a ported script will look for it;
             * what it cannot do is answer for a creature, and saying so is the
             * whole of the fix.
             */
            int GetBaseSpellPower(Api& a, Unit* u)
            {
                uint32 const school = a.Check<uint32>(2);
                if (school >= MAX_SPELL_SCHOOL)
                {
                    luaL_argerror(a.L, 2, "valid SpellSchool expected");
                }

                Player const* player = u->ToPlayer();
                if (!player)
                {
                    luaL_error(a.L, "GetBaseSpellPower is a player's field; "
                                    "this unit does not have one");
                }

                a.Push(player->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS +
                                              school));
                return 1;
            }

            int GetCurrentSpell(Api& a, Unit* u)
            {
                uint32 const type = a.Check<uint32>(2);
                if (type >= CURRENT_MAX_SPELL)
                {
                    luaL_argerror(a.L, 2, "valid CurrentSpellTypes expected");
                }
                a.Push(u->GetCurrentSpell(CurrentSpellTypes(type)));
                return 1;
            }

            int GetAura(Api& a, Unit* u)
            {
                a.Push(u->GetAura(a.Check<uint32>(2), EFFECT_INDEX_0));
                return 1;
            }

            int GetSpeed(Api& a, Unit* u)
            {
                uint32 const type = a.Check<uint32>(2);
                if (type >= MAX_MOVE_TYPE)
                {
                    luaL_argerror(a.L, 2, "valid UnitMoveType expected");
                }
                a.Push(u->GetSpeedRate(UnitMoveType(type)));
                return 1;
            }

            int GetMovementType(Api& a, Unit* u)
            {
                a.Push(uint32(u->GetMotionMaster()
                                  ->GetCurrentMovementGeneratorType()));
                return 1;
            }

            int GetClassAsString(Api& a, Unit* u)
            {
                uint8 const locale = a.Check<uint8>(2, DEFAULT_LOCALE);
                if (locale >= MAX_LOCALE)
                {
                    luaL_argerror(a.L, 2, "valid LocaleConstant expected");
                }

                ChrClassesEntry const* entry =
                    sChrClassesStore.LookupEntry(u->getClass());
                a.Push(entry ? entry->Name_lang[locale] : nullptr);
                return 1;
            }

            int GetRaceAsString(Api& a, Unit* u)
            {
                uint8 const locale = a.Check<uint8>(2, DEFAULT_LOCALE);
                if (locale >= MAX_LOCALE)
                {
                    luaL_argerror(a.L, 2, "valid LocaleConstant expected");
                }

                ChrRacesEntry const* entry =
                    sChrRacesStore.LookupEntry(u->getRace());
                a.Push(entry ? entry->Name_lang[locale] : nullptr);
                return 1;
            }

            int GetFriendlyUnitsInRange(Api& a, Unit* u)
            {
                PushUnitsAround<MaNGOS::AnyFriendlyUnitInObjectRangeCheck>(
                    a, u, SearchRange(a, 2));
                return 1;
            }

            int GetUnfriendlyUnitsInRange(Api& a, Unit* u)
            {
                PushUnitsAround<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck>(
                    a, u, SearchRange(a, 2));
                return 1;
            }

            int CountPctFromCurHealth(Api& a, Unit* u)
            {
                a.Push(u->CountPctFromCurHealth(a.Check<int32>(2)));
                return 1;
            }

            int CountPctFromMaxHealth(Api& a, Unit* u)
            {
                a.Push(u->CountPctFromMaxHealth(a.Check<int32>(2)));
                return 1;
            }

            // ---- changing -------------------------------------------------

            int SetFaction(Api& a, Unit* u)
            {
                u->setFaction(a.Check<uint32>(2));
                return 0;
            }

            int SetLevel(Api& a, Unit* u)
            {
                uint8 const level = a.Check<uint8>(2);
                if (level < 1)
                {
                    luaL_argerror(a.L, 2, "level cannot be below 1");
                }

                if (Player* player = u->ToPlayer())
                {
                    player->GiveLevel(level);
                    player->InitTalentForLevel();
                    player->SetUInt32Value(PLAYER_XP, 0);
                }
                else
                {
                    u->SetLevel(level);
                }
                return 0;
            }

            int SetHealth(Api& a, Unit* u)
            {
                u->SetHealth(a.Check<uint32>(2));
                return 0;
            }

            int SetMaxHealth(Api& a, Unit* u)
            {
                u->SetMaxHealth(a.Check<uint32>(2));
                return 0;
            }

            int SetPower(Api& a, Unit* u)
            {
                uint32 const amount = a.Check<uint32>(2);
                u->SetPower(PowerAt(a, u, 3), amount);
                return 0;
            }

            int ModifyPower(Api& a, Unit* u)
            {
                int32 const amount = a.Check<int32>(2);
                u->ModifyPower(PowerAt(a, u, 3), amount);
                return 0;
            }

            int SetMaxPower(Api& a, Unit* u)
            {
                Powers const power = PowerAt(a, u, 2);
                u->SetMaxPower(power, a.Check<uint32>(3));
                return 0;
            }

            int SetPowerType(Api& a, Unit* u)
            {
                uint32 const type = a.Check<uint32>(2);
                if (type >= uint32(MAX_POWERS))
                {
                    luaL_argerror(a.L, 2, "valid Powers expected");
                }
                u->SetPowerType(Powers(type));
                return 0;
            }

            int SetDisplayId(Api& a, Unit* u)
            {
                u->SetDisplayId(a.Check<uint32>(2));
                return 0;
            }

            int SetNativeDisplayId(Api& a, Unit* u)
            {
                u->SetNativeDisplayId(a.Check<uint32>(2));
                return 0;
            }

            int SetFacing(Api& a, Unit* u)
            {
                u->SetFacingTo(a.Check<float>(2));
                return 0;
            }

            int SetFacingToObject(Api& a, Unit* u)
            {
                if (WorldObject* target = a.CheckObj<WorldObject>(2))
                {
                    u->SetFacingToObject(target);
                }
                return 0;
            }

            int SetSpeed(Api& a, Unit* u)
            {
                uint32 const type = a.Check<uint32>(2);
                float const rate = a.Check<float>(3);
                bool const forced = a.Check<bool>(4, false);

                if (type >= MAX_MOVE_TYPE)
                {
                    luaL_argerror(a.L, 2, "valid UnitMoveType expected");
                }
                u->SetSpeedRate(UnitMoveType(type), rate, forced);
                return 0;
            }

            int SetSheath(Api& a, Unit* u)
            {
                uint32 const state = a.Check<uint32>(2);
                if (state >= MAX_SHEATH_STATE)
                {
                    luaL_argerror(a.L, 2, "valid SheathState expected");
                }
                u->SetSheath(SheathState(state));
                return 0;
            }

            int SetName(Api& a, Unit* u)
            {
                std::string const name = a.Check<std::string>(2);
                if (!name.empty())
                {
                    u->SetName(name);
                }
                return 0;
            }

            int SetPvP(Api& a, Unit* u)
            {
                u->SetPvP(a.Check<bool>(2, true));
                return 0;
            }

            int SetOwnerGUID(Api& a, Unit* u)
            {
                u->SetOwnerGuid(a.Check<ObjectGuid>(2));
                return 0;
            }

            int SetCreatorGUID(Api& a, Unit* u)
            {
                u->SetCreatorGuid(a.Check<ObjectGuid>(2));
                return 0;
            }

            int SetPetGUID(Api& a, Unit* u)
            {
                u->SetPetGuid(a.Check<ObjectGuid>(2));
                return 0;
            }

            int SetWaterWalk(Api& a, Unit* u)
            {
                u->SetWaterWalk(a.Check<bool>(2, true));
                return 0;
            }

            int SetStandState(Api& a, Unit* u)
            {
                u->SetStandState(a.Check<uint8>(2));
                return 0;
            }

            int SetInCombatWith(Api& a, Unit* u)
            {
                if (Unit* enemy = a.CheckObj<Unit>(2))
                {
                    u->SetInCombatWith(enemy);
                }
                return 0;
            }

            int SetRooted(Api& a, Unit* u)
            {
                u->SetRoot(a.Check<bool>(2, true));
                return 0;
            }

            int SetConfused(Api& a, Unit* u)
            {
                u->SetConfused(a.Check<bool>(2, true));
                return 0;
            }

            int SetFeared(Api& a, Unit* u)
            {
                u->SetFeared(a.Check<bool>(2, true));
                return 0;
            }

            int AddUnitState(Api& a, Unit* u)
            {
                u->addUnitState(a.Check<uint32>(2));
                return 0;
            }

            int ClearUnitState(Api& a, Unit* u)
            {
                u->clearUnitState(a.Check<uint32>(2));
                return 0;
            }

            int ClearInCombat(Api& a, Unit* u)
            {
                (void)a;
                u->ClearInCombat();
                return 0;
            }

            int ClearThreatList(Api& a, Unit* u)
            {
                (void)a;
                u->GetThreatManager().clearReferences();
                return 0;
            }

            int Mount(Api& a, Unit* u)
            {
                u->Mount(a.Check<uint32>(2));
                return 0;
            }

            int Dismount(Api& a, Unit* u)
            {
                (void)a;
                if (u->IsMounted())
                {
                    u->Unmount();
                    u->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                }
                return 0;
            }

            int DeMorph(Api& a, Unit* u)
            {
                (void)a;
                u->DeMorph();
                return 0;
            }

            int PerformEmote(Api& a, Unit* u)
            {
                u->HandleEmoteCommand(a.Check<uint32>(2));
                return 0;
            }

            int EmoteState(Api& a, Unit* u)
            {
                u->SetUInt32Value(UNIT_NPC_EMOTESTATE, a.Check<uint32>(2));
                return 0;
            }

            int NearTeleport(Api& a, Unit* u)
            {
                float const x = a.Check<float>(2);
                float const y = a.Check<float>(3);
                float const z = a.Check<float>(4);
                float const o = a.Check<float>(5);
                u->NearTeleportTo(x, y, z, o);
                return 0;
            }

            int AddFlatStatModifier(Api& a, Unit* u)
            {
                uint32 const stat = a.Check<uint32>(2);
                uint8 const modType = a.Check<uint8>(3);
                float const value = a.Check<float>(4);
                bool const apply = a.Check<bool>(5, true);

                if (stat >= MAX_STATS)
                {
                    luaL_argerror(a.L, 2, "valid Stats expected");
                }

                u->HandleStatModifier(UnitMods(UNIT_MOD_STAT_START + stat),
                                      modType == 0 ? BASE_VALUE : TOTAL_VALUE,
                                      value, apply);
                return 0;
            }

            int AddPctStatModifier(Api& a, Unit* u)
            {
                uint32 const stat = a.Check<uint32>(2);
                uint8 const modType = a.Check<uint8>(3);
                float const value = a.Check<float>(4);

                if (stat >= MAX_STATS)
                {
                    luaL_argerror(a.L, 2, "valid Stats expected");
                }

                u->HandleStatModifier(UnitMods(UNIT_MOD_STAT_START + stat),
                                      modType == 0 ? BASE_PCT : TOTAL_PCT,
                                      value, true);
                return 0;
            }

            // ---- fighting -------------------------------------------------

            int Attack(Api& a, Unit* u)
            {
                Unit* who = a.CheckObj<Unit>(2);
                bool const melee = a.Check<bool>(3, false);
                a.Push(who && u->Attack(who, melee));
                return 1;
            }

            int AttackStop(Api& a, Unit* u)
            {
                a.Push(u->AttackStop());
                return 1;
            }

            int CastSpell(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2, false);
                uint32 const spell = a.Check<uint32>(3);
                bool const triggered = a.Check<bool>(4, false);

                if (!sSpellStore.LookupEntry(spell))
                {
                    return 0;
                }
                u->CastSpell(target, spell, triggered);
                return 0;
            }

            int CastCustomSpell(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2, false);
                uint32 const spell = a.Check<uint32>(3);
                bool const triggered = a.Check<bool>(4, false);

                bool const hasBp0 = !a.IsNoneOrNil(5);
                int32 bp0 = a.Check<int32>(5, 0);
                bool const hasBp1 = !a.IsNoneOrNil(6);
                int32 bp1 = a.Check<int32>(6, 0);
                bool const hasBp2 = !a.IsNoneOrNil(7);
                int32 bp2 = a.Check<int32>(7, 0);

                Item* castItem = a.CheckObj<Item>(8, false);
                ObjectGuid const original =
                    a.IsNoneOrNil(9) ? ObjectGuid() : a.Check<ObjectGuid>(9);

                if (!sSpellStore.LookupEntry(spell))
                {
                    return 0;
                }

                u->CastCustomSpell(target, spell, hasBp0 ? &bp0 : nullptr,
                                   hasBp1 ? &bp1 : nullptr,
                                   hasBp2 ? &bp2 : nullptr, triggered,
                                   castItem, nullptr, original);
                return 0;
            }

            /// Cast at a point rather than at a target -- "area of effect at
            /// this floor tile", which is how ground-targeted spells work.
            int CastSpellAoF(Api& a, Unit* u)
            {
                float const x = a.Check<float>(2);
                float const y = a.Check<float>(3);
                float const z = a.Check<float>(4);
                uint32 const spell = a.Check<uint32>(5);
                bool const triggered = a.Check<bool>(6, true);

                if (!sSpellStore.LookupEntry(spell))
                {
                    return 0;
                }
                u->CastSpell(x, y, z, spell, triggered);
                return 0;
            }

            int StopSpellCast(Api& a, Unit* u)
            {
                u->CastStop(a.Check<uint32>(2, 0));
                return 0;
            }

            int InterruptSpell(Api& a, Unit* u)
            {
                uint32 const type = a.Check<uint32>(2);
                bool const delayed = a.Check<bool>(3, true);

                if (type >= CURRENT_MAX_SPELL)
                {
                    luaL_argerror(a.L, 2, "valid CurrentSpellTypes expected");
                }
                u->InterruptSpell(CurrentSpellTypes(type), delayed);
                return 0;
            }

            int AddAura(Api& a, Unit* u)
            {
                uint32 const spell = a.Check<uint32>(2);
                Unit* target = a.CheckObj<Unit>(3);
                if (!target)
                {
                    return 0;
                }

                SpellEntry const* entry = sSpellStore.LookupEntry(spell);
                if (!entry)
                {
                    return 0;
                }
                if (!IsSpellAppliesAura(entry) &&
                    !IsSpellHaveEffect(entry, SPELL_EFFECT_PERSISTENT_AREA_AURA))
                {
                    return 0;
                }

                SpellAuraHolder* holder =
                    CreateSpellAuraHolder(entry, target, u);
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    uint8 const effect = entry->Effect[i];
                    if (effect >= TOTAL_SPELL_EFFECTS)
                    {
                        continue;
                    }
                    if (IsAreaAuraEffect(effect) ||
                        effect == SPELL_EFFECT_APPLY_AURA ||
                        effect == SPELL_EFFECT_PERSISTENT_AREA_AURA)
                    {
                        Aura* aura = CreateAura(entry, SpellEffectIndex(i),
                                                nullptr, holder, target);
                        holder->AddAura(aura, SpellEffectIndex(i));
                    }
                }

                a.Push(target->AddSpellAuraHolder(holder));
                return 1;
            }

            int RemoveAura(Api& a, Unit* u)
            {
                u->RemoveAurasDueToSpell(a.Check<uint32>(2));
                return 0;
            }

            int RemoveAllAuras(Api& a, Unit* u)
            {
                (void)a;
                u->RemoveAllAuras();
                return 0;
            }

            int RemoveArenaAuras(Api& a, Unit* u)
            {
                (void)a;
                u->RemoveArenaAuras();
                return 0;
            }

            int DealDamage(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2);
                uint32 damage = a.Check<uint32>(3);
                bool const durabilityLoss = a.Check<bool>(4, true);
                uint32 const school = a.Check<uint32>(5, MAX_SPELL_SCHOOL);
                uint32 const spell = a.Check<uint32>(6, 0);

                if (!target)
                {
                    return 0;
                }
                if (school > MAX_SPELL_SCHOOL)
                {
                    luaL_argerror(a.L, 5, "valid SpellSchool expected");
                }

                // No school named: raw damage, no mitigation, reported to the
                // client as a plain swing.
                if (school == MAX_SPELL_SCHOOL)
                {
                    u->DealDamage(target, damage, nullptr, DIRECT_DAMAGE,
                                  SPELL_SCHOOL_MASK_NORMAL, nullptr,
                                  durabilityLoss);
                    u->SendAttackStateUpdate(HITINFO_NORMALSWING2, target,
                                             SPELL_SCHOOL_MASK_NORMAL, damage,
                                             0, 0, VICTIMSTATE_NORMAL, 0);
                    return 0;
                }

                SpellSchoolMask const mask = SpellSchoolMask(1 << school);
                if (mask & SPELL_SCHOOL_MASK_NORMAL)
                {
                    damage = u->CalcArmorReducedDamage(target, damage);
                }

                if (!spell)
                {
                    uint32 absorb = 0;
                    uint32 resist = 0;
                    target->CalculateDamageAbsorbAndResist(
                        u, mask, SPELL_DIRECT_DAMAGE, damage, &absorb,
                        &resist);

                    damage = (damage <= absorb + resist)
                                 ? 0
                                 : damage - absorb - resist;

                    u->DealDamageMods(target, damage, &absorb);
                    u->DealDamage(target, damage, nullptr, DIRECT_DAMAGE, mask,
                                  nullptr, false);
                    u->SendAttackStateUpdate(HITINFO_NORMALSWING2, target,
                                             mask, damage, absorb, resist,
                                             VICTIMSTATE_NORMAL, 0);
                    return 0;
                }

                u->SpellNonMeleeDamageLog(target, spell, damage);
                return 0;
            }

            int DealHeal(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2);
                uint32 const spell = a.Check<uint32>(3);
                uint32 const amount = a.Check<uint32>(4);
                bool const critical = a.Check<bool>(5, false);

                SpellEntry const* entry = sSpellStore.LookupEntry(spell);
                if (target && entry)
                {
                    u->DealHeal(target, amount, entry, critical);
                }
                return 0;
            }

            int Kill(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2);
                bool const durabilityLoss = a.Check<bool>(3, true);

                if (target)
                {
                    u->DealDamage(target, target->GetHealth(), nullptr,
                                  DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL,
                                  nullptr, durabilityLoss);
                }
                return 0;
            }

            int AddThreat(Api& a, Unit* u)
            {
                Unit* victim = a.CheckObj<Unit>(2);
                float const threat = a.Check<float>(3);
                uint32 const spell = a.Check<uint32>(4, 0);

                if (!victim)
                {
                    return 0;
                }

                SpellEntry const* entry = sSpellStore.LookupEntry(spell);
                u->AddThreat(victim, threat, false,
                             entry ? SpellSchoolMask(entry->SchoolMask)
                                   : SPELL_SCHOOL_MASK_NONE,
                             entry);
                return 0;
            }

            // ---- moving ---------------------------------------------------

            int MoveStop(Api& a, Unit* u)
            {
                (void)a;
                u->StopMoving();
                return 0;
            }

            int MoveIdle(Api& a, Unit* u)
            {
                (void)a;
                u->GetMotionMaster()->MoveIdle();
                return 0;
            }

            int MoveHome(Api& a, Unit* u)
            {
                (void)a;
                u->GetMotionMaster()->MoveTargetedHome();
                return 0;
            }

            int MoveConfused(Api& a, Unit* u)
            {
                (void)a;
                u->GetMotionMaster()->MoveConfused();
                return 0;
            }

            int MoveExpire(Api& a, Unit* u)
            {
                u->GetMotionMaster()->MovementExpired(a.Check<bool>(2, true));
                return 0;
            }

            int MoveClear(Api& a, Unit* u)
            {
                u->GetMotionMaster()->Clear(a.Check<bool>(2, true));
                return 0;
            }

            int MoveRandom(Api& a, Unit* u)
            {
                float const radius = a.Check<float>(2);
                Geometry::Placement const& where = u->Where();
                u->GetMotionMaster()->MoveRandomAroundPoint(
                    where.X(), where.Y(), where.Z(), radius);
                return 0;
            }

            int MoveFollow(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2);
                float const dist = a.Check<float>(3, 0.0f);
                float const angle = a.Check<float>(4, 0.0f);

                if (target)
                {
                    u->GetMotionMaster()->MoveFollow(target, dist, angle);
                }
                return 0;
            }

            int MoveChase(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2);
                float const dist = a.Check<float>(3, 0.0f);
                float const angle = a.Check<float>(4, 0.0f);

                if (target)
                {
                    u->GetMotionMaster()->MoveChase(target, dist, angle);
                }
                return 0;
            }

            int MoveFleeing(Api& a, Unit* u)
            {
                Unit* target = a.CheckObj<Unit>(2);
                uint32 const time = a.Check<uint32>(3, 0);

                if (target)
                {
                    u->GetMotionMaster()->MoveFleeing(target, time);
                }
                return 0;
            }

            int MoveTo(Api& a, Unit* u)
            {
                uint32 const id = a.Check<uint32>(2);
                float const x = a.Check<float>(3);
                float const y = a.Check<float>(4);
                float const z = a.Check<float>(5);
                bool const generatePath = a.Check<bool>(6, true);

                u->GetMotionMaster()->MovePoint(id, x, y, z, generatePath);
                return 0;
            }

            // ---- speaking -------------------------------------------------

            int SendUnitSay(Api& a, Unit* u)
            {
                std::string const msg = a.Check<std::string>(2);
                uint32 const language = a.Check<uint32>(3);

                if (!msg.empty())
                {
                    u->MonsterSay(msg.c_str(), language, u);
                }
                return 0;
            }

            int SendUnitYell(Api& a, Unit* u)
            {
                std::string const msg = a.Check<std::string>(2);
                uint32 const language = a.Check<uint32>(3);

                if (!msg.empty())
                {
                    u->MonsterYell(msg.c_str(), language, u);
                }
                return 0;
            }

            int SendUnitEmote(Api& a, Unit* u)
            {
                std::string const msg = a.Check<std::string>(2);
                Unit* receiver = a.CheckObj<Unit>(3, false);
                bool const bossEmote = a.Check<bool>(4, false);

                if (!msg.empty())
                {
                    u->MonsterTextEmote(msg.c_str(), receiver, bossEmote);
                }
                return 0;
            }

            /**
             * Argument 3 is skipped, and deliberately.
             *
             * Eluna's signature is (msg, lang, receiver, bossWhisper) and the
             * argument positions are kept so a script ported from it reads
             * unchanged -- but a whisper from a unit is not spoken in a
             * language in this core, MonsterWhisper takes none, and inventing
             * one would be a parameter that silently does nothing. The gap is
             * real; it is here so the next reader does not take it for a typo
             * and "fix" it by shifting everything down one.
             */
            int SendUnitWhisper(Api& a, Unit* u)
            {
                std::string const msg = a.Check<std::string>(2);
                Player* receiver = a.CheckObj<Player>(4);
                bool const bossWhisper = a.Check<bool>(5, false);

                if (receiver && !msg.empty())
                {
                    u->MonsterWhisper(msg.c_str(), receiver, bossWhisper);
                }
                return 0;
            }

            int SendChatMessageToPlayer(Api& a, Unit* u)
            {
                uint8 const type = a.Check<uint8>(2);
                uint32 const language = a.Check<uint32>(3);
                std::string const msg = a.Check<std::string>(4);
                Player* target = a.CheckObj<Player>(5);

                if (type >= MAX_CHAT_MSG_TYPE)
                {
                    luaL_argerror(a.L, 2, "valid ChatMsg expected");
                }
                if (language >= LANGUAGES_COUNT)
                {
                    luaL_argerror(a.L, 3, "valid Language expected");
                }
                if (!target || !target->GetSession())
                {
                    return 0;
                }

                WorldPacket data;
                ChatHandler::BuildChatPacket(
                    data, ChatMsg(type), msg.c_str(), Language(language),
                    CHAT_TAG_NONE, u->GetObjectGuid(), u->GetName(),
                    target->GetObjectGuid(), target->GetName());
                target->GetSession()->SendPacket(&data);
                return 0;
            }
        }

        MethodEntry const* UnitMethods(std::size_t& count)
        {
#define M(name)                                                               \
            { #name, [](Api& a, void* self) -> int                            \
                { return name(a, static_cast<Unit*>(self)); } }

            static MethodEntry const table[] =
            {
                M(IsAlive), M(IsDead), M(IsDying), M(IsInCombat), M(IsMounted),
                M(IsStandState), M(IsFullHealth), M(IsStopped), M(IsInWater),
                M(IsUnderWater), M(IsCharmed), M(IsPvPFlagged), M(IsBanker),
                M(IsBattleMaster), M(IsArmorer), M(IsVendor), M(IsAuctioneer),
                M(IsGuildMaster), M(IsInnkeeper), M(IsTrainer), M(IsGossip),
                M(IsTaxi), M(IsSpiritHealer), M(IsSpiritGuide),
                M(IsSpiritService), M(IsQuestGiver), M(IsTabardDesigner),
                M(IsServiceProvider), M(IsAttackingPlayer), M(IsRooted),
                M(IsCasting), M(IsInAccessiblePlaceFor), M(CanModifyStats),
                M(HasAura), M(HasUnitState), M(HealthBelowPct),
                M(HealthAbovePct),

                M(GetLevel), M(GetHealth), M(GetMaxHealth), M(GetHealthPct),
                M(GetPower), M(GetMaxPower), M(GetPowerPct), M(GetPowerType),
                M(GetGender), M(GetRace), M(GetClass), M(GetRaceMask),
                M(GetClassMask), M(GetRaceAsString), M(GetClassAsString),
                M(GetFaction), M(GetCreatureType), M(GetDisplayId),
                M(GetNativeDisplayId), M(GetMountId), M(GetStandState),
                M(GetOwner), M(GetVictim), M(GetOwnerGUID), M(GetCreatorGUID),
                M(GetCharmerGUID), M(GetCharmGUID), M(GetPetGUID),
                M(GetControllerGUID), M(GetControllerGUIDS), M(GetStat),
                M(GetBaseSpellPower), M(GetCurrentSpell), M(GetAura),
                M(GetSpeed), M(GetMovementType), M(GetFriendlyUnitsInRange),
                M(GetUnfriendlyUnitsInRange), M(CountPctFromCurHealth),
                M(CountPctFromMaxHealth),

                // Eluna's name for the pet slot, kept so its scripts read.
                { "GetMinionGUID", [](Api& a, void* self) -> int
                    { return GetPetGUID(a, static_cast<Unit*>(self)); } },
                { "SetMinionGUID", [](Api& a, void* self) -> int
                    { return SetPetGUID(a, static_cast<Unit*>(self)); } },

                M(SetFaction), M(SetLevel), M(SetHealth), M(SetMaxHealth),
                M(SetPower), M(SetMaxPower), M(SetPowerType), M(ModifyPower),
                M(SetDisplayId), M(SetNativeDisplayId), M(SetFacing),
                M(SetFacingToObject), M(SetSpeed), M(SetSheath), M(SetName),
                M(SetPvP), M(SetOwnerGUID), M(SetCreatorGUID), M(SetPetGUID),
                M(SetWaterWalk), M(SetStandState), M(SetInCombatWith),
                M(SetRooted), M(SetConfused), M(SetFeared),
                M(AddFlatStatModifier), M(AddPctStatModifier),

                M(AddUnitState), M(ClearUnitState), M(ClearInCombat),
                M(ClearThreatList), M(Mount), M(Dismount), M(DeMorph),
                M(PerformEmote), M(EmoteState), M(NearTeleport),

                M(Attack), M(AttackStop), M(CastSpell), M(CastCustomSpell),
                M(CastSpellAoF), M(StopSpellCast), M(InterruptSpell),
                M(AddAura), M(RemoveAura), M(RemoveAllAuras),
                M(RemoveArenaAuras), M(DealDamage), M(DealHeal), M(Kill),
                M(AddThreat),

                M(MoveStop), M(MoveIdle), M(MoveHome), M(MoveConfused),
                M(MoveExpire), M(MoveClear), M(MoveRandom), M(MoveFollow),
                M(MoveChase), M(MoveFleeing), M(MoveTo),

                M(SendUnitSay), M(SendUnitYell), M(SendUnitEmote),
                M(SendUnitWhisper), M(SendChatMessageToPlayer),

                // Wrath mechanics. Not gaps: vehicles, the critter slot,
                // free-for-all flagging, sanctuary and the jump movement
                // generator do not exist in 2.4.3, and a script asking for
                // them is asking for something the client would not honour.
                { "GetVehicle", nullptr },
                { "GetVehicleKit", nullptr },
                { "GetCritterGUID", nullptr },
                { "SetCritterGUID", nullptr },
                { "SetFFA", nullptr },
                { "SetSanctuary", nullptr },
                { "MoveJump", nullptr },
                { "IsOnVehicle", nullptr },

                // Never implemented in Eluna for this lineage either. Kept by
                // name so a script written against its manual is told which
                // of the two it is.
                { "SetStunned", nullptr },
                { "SetCanFly", nullptr },
                { "SetVisible", nullptr },
                { "IsVisible", nullptr },
                { "IsMoving", nullptr },
                { "IsFlying", nullptr },
                { "RestoreDisplayId", nullptr },
                { "RestoreFaction", nullptr },
                { "RemoveBindSightAuras", nullptr },
                { "RemoveCharmAuras", nullptr },
                { "DisableMelee", nullptr },
                { "SummonGuardian", nullptr },
                { "SetImmuneTo", nullptr },
            };

#undef M

            count = sizeof(table) / sizeof(table[0]);
            return table;
        }
    }
}
