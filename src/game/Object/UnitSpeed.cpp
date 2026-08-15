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



#include "Unit.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "SpellMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "Creature.h"
#include "Spell.h"
#include "Group.h"
#include "SpellAuras.h"
#include "MapManager.h"
#include "CreatureAI.h"
#include "TemporarySummon.h"
#include "Formulas.h"
#include "Pet.h"
#include "Util.h"
#include "Totem.h"
#include "BattleGround/BattleGround.h"
#include "InstanceData.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "MapPersistentStateMgr.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "MovementGenerator.h"
#include "movement/MoveSplineInit.h"
#include "CreatureLinkingMgr.h"
#include "GameTime.h"

/**
 * @brief Recalculates movement speed for a move type from active modifiers.
 *
 * @param mtype The movement type to update.
 * @param forced True to send a forced speed change packet to players.
 * @param ratio Additional multiplier applied after recalculation.
 */
void Unit::UpdateSpeed(UnitMoveType mtype, bool forced, float ratio)
{
    // not in combat pet have same speed as owner
    switch (mtype)
    {
        case MOVE_RUN:
        case MOVE_WALK:
        case MOVE_SWIM:
            if (GetTypeId() == TYPEID_UNIT && ((Creature*)this)->IsPet() && hasUnitState(UNIT_STAT_FOLLOW))
            {
                if (Unit* owner = GetOwner())
                {
                    SetSpeedRate(mtype, owner->GetSpeedRate(mtype), forced);
                    return;
                }
            }
            break;
        default:
            break;
    }

    SpeedFactors factors;
    factors.ratio = ratio;

    switch (mtype)
    {
        case MOVE_WALK:
            break;
        case MOVE_RUN:
        {
            if (IsMounted()) // Use on mount auras
            {
                factors.mainMod       = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED);
                factors.stackBonus    = GetTotalAuraMultiplier(SPELL_AURA_MOD_MOUNTED_SPEED_ALWAYS);
                factors.nonStackBonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_MOUNTED_SPEED_NOT_STACK)) / 100.0f;
            }
            else
            {
                factors.mainMod       = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_SPEED);
                factors.stackBonus    = GetTotalAuraMultiplier(SPELL_AURA_MOD_SPEED_ALWAYS);
                factors.nonStackBonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_SPEED_NOT_STACK)) / 100.0f;
            }
            break;
        }
        case MOVE_RUN_BACK:
            return;
        case MOVE_SWIM:
        {
            factors.mainMod = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_SWIM_SPEED);
            break;
        }
        case MOVE_SWIM_BACK:
            return;
        case MOVE_FLIGHT:
        {
            if (IsMounted()) // Use on mount auras
            {
                factors.mainMod       = GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED);
                factors.stackBonus    = GetTotalAuraMultiplier(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED_STACKING);
                factors.nonStackBonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED_NOT_STACKING)) / 100.0f;
            }
            else             // Use not mount (shapeshift for example) auras (should stack)
            {
                factors.mainMod       = GetTotalAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED);
                factors.stackBonus    = GetTotalAuraMultiplier(SPELL_AURA_MOD_FLIGHT_SPEED_STACKING);
                factors.nonStackBonus = (100.0f + GetMaxPositiveAuraModifier(SPELL_AURA_MOD_FLIGHT_SPEED_NOT_STACKING)) / 100.0f;
            }
            break;
        }
        case MOVE_FLIGHT_BACK:
            return;
        default:
            sLog.outError("Unit::UpdateSpeed: Unsupported move type (%d)", mtype);
            return;
    }

    // Aura 191 is a ceiling in yards per second rather than a percentage, and
    // only the three forward move types honour it.
    factors.normalization = GetMaxPositiveAuraModifier(SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED);

    factors.slow = GetMaxNegativeAuraModifier(SPELL_AURA_MOD_DECREASE_SPEED);

    if (GetTypeId() == TYPEID_UNIT)
    {
        // a mob that went looking for help moves at a third less, so the pull
        // reads as deliberate rather than as a charge
        if (((Creature*)this)->HasSearchedAssistance())
        {
            factors.stateScale = 0.66f;
        }

        switch (mtype)
        {
            case MOVE_RUN:
                factors.creatureRate = ((Creature*)this)->GetCreatureInfo()->SpeedRun;
                break;
            case MOVE_WALK:
                factors.creatureRate = ((Creature*)this)->GetCreatureInfo()->SpeedWalk;
                break;
            default:
                break;
        }
    }
    else if (GetDeathState() == CORPSE)
    {
        factors.stateScale = sWorld.getConfig(((Player*)this)->InBattleGround()
                                              ? CONFIG_FLOAT_GHOST_RUN_SPEED_BG
                                              : CONFIG_FLOAT_GHOST_RUN_SPEED_WORLD);
    }

    SetSpeedRate(mtype, ComposeSpeedRate(mtype, factors), forced);
}

/**
 * @brief Gets the current movement speed for a move type.
 *
 * @param mtype The movement type.
 * @return The resulting speed value.
 */
float Unit::GetSpeed(UnitMoveType mtype) const
{
    return m_speeds.Speed(mtype);
}

struct SetSpeedRateHelper
{
    explicit SetSpeedRateHelper(UnitMoveType _mtype, bool _forced) : mtype(_mtype), forced(_forced) {}
    void operator()(Unit* unit) const { unit->UpdateSpeed(mtype, forced); }
    UnitMoveType mtype;
    bool forced;
};

/**
 * @brief Sets the speed rate for a move type and broadcasts the change.
 *
 * @param mtype The movement type.
 * @param rate The new speed rate.
 * @param forced True to use forced speed change handling for players.
 */
void Unit::SetSpeedRate(UnitMoveType mtype, float rate, bool forced)
{
    // Everyone is told only when the rate actually moved
    if (m_speeds.Store(mtype, rate))
    {
        PropagateSpeedChange();

        const uint16 SetSpeed2Opc_table[MAX_MOVE_TYPE][2] =
        {
            {SMSG_FORCE_WALK_SPEED_CHANGE,        SMSG_SPLINE_SET_WALK_SPEED},
            {SMSG_FORCE_RUN_SPEED_CHANGE,         SMSG_SPLINE_SET_RUN_SPEED},
            {SMSG_FORCE_RUN_BACK_SPEED_CHANGE,    SMSG_SPLINE_SET_RUN_BACK_SPEED},
            {SMSG_FORCE_SWIM_SPEED_CHANGE,        SMSG_SPLINE_SET_SWIM_SPEED},
            {SMSG_FORCE_SWIM_BACK_SPEED_CHANGE,   SMSG_SPLINE_SET_SWIM_BACK_SPEED},
            {SMSG_FORCE_TURN_RATE_CHANGE,         SMSG_SPLINE_SET_TURN_RATE},
            {SMSG_FORCE_FLIGHT_SPEED_CHANGE,      SMSG_SPLINE_SET_FLIGHT_SPEED},
            {SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE, SMSG_SPLINE_SET_FLIGHT_BACK_SPEED},
        };

        if (forced && GetTypeId() == TYPEID_PLAYER)
        {
            // register forced speed changes for WorldSession::HandleForceSpeedChangeAck
            // and do it only for real sent packets and use run for run/mounted as client expected
            ++((Player*)this)->m_forced_speed_changes[mtype];

            WorldPacket data(SetSpeed2Opc_table[mtype][0], 18);
            data << GetPackGUID();
            data << (uint32)0;                              // moveEvent, NUM_PMOVE_EVTS = 0x39
            if (mtype == MOVE_RUN)
            {
                data << uint8(0);                           // new 2.1.0
            }
            data << float(GetSpeed(mtype));
            ((Player*)this)->GetSession()->SendPacket(&data);
        }
        WorldPacket data(SetSpeed2Opc_table[mtype][1], 12);
        data << GetPackGUID();
        data << float(GetSpeed(mtype));
        SendMessageToSet(&data, false);
    }

    CallForAllControlledUnits(SetSpeedRateHelper(mtype, forced), CONTROLLED_PET | CONTROLLED_GUARDIANS | CONTROLLED_CHARM | CONTROLLED_MINIPET);
}

/**
 * @brief Applies or removes the feared state.
 *
 * @param apply True to apply fear; false to remove it.
 * @param casterGuid The caster responsible for the effect.
 * @param spellID The spell that caused the effect.
 * @param time The remaining flee duration.
 */
void Unit::SetFeared(bool apply, ObjectGuid casterGuid, uint32 spellID, uint32 time)
{
    if (apply)
    {
        if (HasAuraType(SPELL_AURA_PREVENTS_FLEEING))
        {
            return;
        }

        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_FLEEING);

        GetMotionMaster()->MovementExpired(false);
        CastStop(GetObjectGuid() == casterGuid ? spellID : 0);

        if (GetTypeId() == TYPEID_UNIT)
        {
            SetTargetGuid(ObjectGuid());                    // creature feared loose its target
        }

        Unit* caster = IsInWorld() ?  GetMap()->GetUnit(casterGuid) : NULL;

        GetMotionMaster()->MoveFleeing(caster, time);       // caster==NULL processed in MoveFleeing
    }
    else
    {
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_FLEEING);

        GetMotionMaster()->MovementExpired(false);

        if (GetTypeId() != TYPEID_PLAYER && IsAlive())
        {
            Creature* c = ((Creature*)this);
            // restore appropriate movement generator
            if (getVictim())
            {
                SetTargetGuid(getVictim()->GetObjectGuid());  // restore target
                GetMotionMaster()->MoveChase(getVictim());
            }
            else
            {
                GetMotionMaster()->Initialize();
            }

            // attack caster if can
            if (Unit* caster = IsInWorld() ? GetMap()->GetUnit(casterGuid) : NULL)
            {
                c->AttackedBy(caster);
            }
        }
    }

    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->SetClientControl(this, !apply);
    }
}

/**
 * @brief Applies or removes the confused state.
 *
 * @param apply True to apply confusion; false to remove it.
 * @param casterGuid The caster responsible for the effect.
 * @param spellID The spell that caused the effect.
 */
void Unit::SetConfused(bool apply, ObjectGuid casterGuid, uint32 spellID)
{
    if (apply)
    {
         SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_CONFUSED);

         GetMotionMaster()->MovementExpired(false);
         CastStop(GetObjectGuid() == casterGuid ? spellID : 0);

         if (GetTypeId() == TYPEID_UNIT)
         {
            SetTargetGuid(ObjectGuid());
            GetMotionMaster()->MoveConfused();
         }
    }
    else
    {
         RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_CONFUSED);

         GetMotionMaster()->MovementExpired(false);

         if (GetTypeId() != TYPEID_PLAYER && IsAlive())
         {
         // restore appropriate movement generator
                if (getVictim())
                {
                       SetTargetGuid(getVictim()->GetObjectGuid());
                       GetMotionMaster()->MoveChase(getVictim());
                 }
                 else
                 {
                     GetMotionMaster()->Initialize();
                 }
          }
    }

    if (GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->SetClientControl(this, !apply);
    }
}

/**
 * @brief Applies or removes feign death state handling.
 *
 * @param apply True to enable feign death; false to clear it.
 * @param casterGuid The caster responsible for the effect.
 */
void Unit::SetFeignDeath(bool apply, ObjectGuid casterGuid /*= ObjectGuid()*/)
{
    if (apply)
    {
        /*
        WorldPacket data(SMSG_FEIGN_DEATH_RESISTED, 9);
        data<<GetGUID();
        data<<uint8(0);
        SendMessageToSet(&data,true);
        */

        if (GetTypeId() != TYPEID_PLAYER)
        {
            StopMoving();
        }
        else
        {
            ((Player*)this)->m_movementInfo.SetMovementFlags(MOVEFLAG_NONE);
        }

        // blizz like 2.0.x
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_29);
        // blizz like 2.0.x
        SetFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);
        // blizz like 2.0.x
        SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);

        addUnitState(UNIT_STAT_DIED);
        CombatStop();
        RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_IMMUNE_OR_LOST_SELECTION);

        // prevent interrupt message
        if (casterGuid == GetObjectGuid())
        {
            FinishSpell(CURRENT_GENERIC_SPELL, false);
        }
        InterruptNonMeleeSpells(true);
        GetHostileRefManager().deleteReferences();
    }
    else
    {
        /*
        WorldPacket data(SMSG_FEIGN_DEATH_RESISTED, 9);
        data<<GetGUID();
        data<<uint8(1);
        SendMessageToSet(&data,true);
        */
        // blizz like 2.0.x
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_29);
        // blizz like 2.0.x
        RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FEIGN_DEATH);
        // blizz like 2.0.x
        RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);

        clearUnitState(UNIT_STAT_DIED);

        if (GetTypeId() != TYPEID_PLAYER && IsAlive())
        {
            // restore appropriate movement generator
            if (getVictim())
            {
                GetMotionMaster()->MoveChase(getVictim());
            }
            else
            {
                GetMotionMaster()->Initialize();
            }
        }
    }
}
