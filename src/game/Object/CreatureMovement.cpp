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



#include "Creature.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "LivingWorldAnchorPolicy.h"
#include "Database/DatabaseEnv.h"
#include "World.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "SQLStorages.h"
#include "SpellMgr.h"
#include "GossipDef.h"
#include "Player.h"
#include "GameEventMgr.h"
#include "PoolManager.h"
#include "Log.h"
#include "LootMgr.h"
#include "MapManager.h"
#include "CreatureAI.h"
#include "CreatureAISelector.h"
#include "InstanceData.h"
#include "MapPersistentStateMgr.h"
#include "BattleGround/BattleGroundMgr.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "Spell.h"
#include "Util.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "movement/MoveSplineInit.h"
#include "CreatureLinkingMgr.h"
#include "DisableMgr.h"
#include "Policies/Singleton.h"

/**
 * @brief Enables or disables walk mode for the creature.
 *
 * @param enable true to walk; false to run.
 * @param asDefault true to also update the default running state.
 */
void Creature::SetWalk(bool enable, bool asDefault)
{
    if (asDefault)
    {
        if (enable)
        {
            clearUnitState(UNIT_STAT_RUNNING);
        }
        else
        {
            addUnitState(UNIT_STAT_RUNNING);
        }
    }

    // Nothing changed?
    if (enable == m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE))
    {
        return;
    }

    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_WALK_MODE);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_WALK_MODE);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_SET_WALK_MODE : SMSG_SPLINE_MOVE_SET_RUN_MODE, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
}

/**
 * @brief Enables or disables levitation movement flags.
 *
 * @param enable true to levitate; false to clear the flag.
 */
void Creature::SetLevitate(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_LEVITATING);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_LEVITATING);
    }

    // TODO: there should be analogic opcode for 2.43
    // WorldPacket data(enable ? SMSG_SPLINE_MOVE_GRAVITY_DISABLE : SMSG_SPLINE_MOVE_GRAVITY_ENABLE, 9);
    // data << GetPackGUID();
    // SendMessageToSet(&data, true);
}

/**
 * @brief Whether the client should be shown the swim animation.
 *
 * THE SAME DISCRIMINANT THE ROUTER SEATS BY, asked again where the packet is sent.
 * Nav::SeatHeight puts a body with feet on the floor -- on sand, on a seabed, under
 * thirty yards of ocean if that is where the floor is -- and puts everything else in
 * the column between the floor and the skin. A body on the floor is walking, whatever
 * stands above it; a body in the column is swimming. One question in both places is
 * what stops the route and the animation describing two different creatures.
 *
 * A pet is the one walker that rides the surface, because it follows its owner out
 * over water no floor can reach.
 *
 * The walker answers without touching the terrain, which is the whole population that
 * is relocated every tick and never swims.
 */
bool Creature::ShouldSwim() const
{
    if (CanWalk() && !RidesWater())
    {
        return false;
    }

    // Any liquid, not only water: an ooze in slime and an elemental in lava are
    // swimming as far as the client is concerned, and IsInWater asks about all of them.
    return IsInWater();
}

/**
 * @brief Bring the swim flag in line with where the body actually is.
 *
 * Called on every relocation. The flag used to be decided once, at spawn, from the
 * InhabitType and the spawn point -- so a creature that spawned in water carried it
 * onto dry land for the rest of its life, and one that spawned ashore swam without it.
 */
void Creature::UpdateSwimState()
{
    const bool swim = ShouldSwim();
    if (swim != IsSwimming())
    {
        SetSwim(swim);
    }
}

/**
 * @brief Enables or disables swim movement flags and broadcasts the change.
 *
 * @param enable true to swim; false to stop swimming.
 */
void Creature::SetSwim(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_SWIMMING);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_SWIMMING);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_START_SWIM : SMSG_SPLINE_MOVE_STOP_SWIM);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
}

/**
 * @brief Placeholder for enabling or disabling flight.
 *
 * @param enable Unused flight toggle.
 */
void Creature::SetCanFly(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_CAN_FLY);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_CAN_FLY);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_SET_FLYING : SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
}

/**
 * @brief Enables or disables feather-fall movement behavior.
 *
 * @param enable true to enable feather fall; false to restore normal falling.
 */
void Creature::SetFeatherFall(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_SAFE_FALL);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_SAFE_FALL);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_FEATHER_FALL : SMSG_SPLINE_MOVE_NORMAL_FALL);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
}

/**
 * @brief Enables or disables hover movement behavior.
 *
 * @param enable true to hover; false to unset hover.
 */
void Creature::SetHover(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_HOVER);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_HOVER);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_SET_HOVER : SMSG_SPLINE_MOVE_UNSET_HOVER, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, false);
}

/**
 * @brief Enables or disables root movement behavior.
 *
 * @param enable true to root; false to unroot.
 */
void Creature::SetRoot(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_ROOT);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_ROOT);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_ROOT : SMSG_SPLINE_MOVE_UNROOT, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
}

/**
 * @brief Enables or disables water-walking behavior.
 *
 * @param enable true to water walk; false to restore land walking.
 */
void Creature::SetWaterWalk(bool enable)
{
    if (enable)
    {
        m_movementInfo.AddMovementFlag(MOVEFLAG_WATERWALKING);
    }
    else
    {
        m_movementInfo.RemoveMovementFlag(MOVEFLAG_WATERWALKING);
    }

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_WATER_WALK : SMSG_SPLINE_MOVE_LAND_WALK, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
}
