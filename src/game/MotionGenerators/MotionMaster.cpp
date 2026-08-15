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
#include "MotionMaster.h"
#include "ConfusedMovementGenerator.h"
#include "FleeingMovementGenerator.h"
#include "HomeMovementGenerator.h"
#include "IdleMovementGenerator.h"
#include "PointMovementGenerator.h"
#include "TargetedMovementGenerator.h"
#include "WaypointMovementGenerator.h"
#include "RandomMovementGenerator.h"
#include "movement/MoveSplineInit.h"
#include "Map.h"
#include "CreatureAISelector.h"
#include "Creature.h"
#include "CreatureLinkingMgr.h"
#include "Pet.h"
#include "DBCStores.h"
#include "Log.h"

#include <cassert>
#include <cmath>
#include <sstream>

/**
 * @brief Checks if the movement generator is static (idle movement).
 * @param mv Pointer to the movement generator.
 * @return True if the movement generator is static, false otherwise.
 */
inline static bool isStatic(MovementGenerator* mv)
{
    return (mv == &si_idleMovement);
}

/**
 * @brief Chase and follow, which stack on one another and must be dropped together.
 *
 * Expiring a chase that was sitting on another chase used to leave the creature
 * pursuing what it had just been told to stop pursuing. The rule was a loop over two
 * enum values buried inside expire; naming it does not change it, but it stops it
 * reading like an accident.
 */
inline static bool isTargeted(MovementGenerator* mv)
{
    const MovementGeneratorType type = mv->GetMovementGeneratorType();
    return type == CHASE_MOTION_TYPE || type == FOLLOW_MOTION_TYPE;
}


/**
 * @brief What a generator's claim on the unit is worth.
 *
 * The table is the whole policy, in one readable place, where it used to be an ordering
 * implied by the sequence of pushes and two special cases inside Mutate.
 */
inline static Helm::Rank rankOf(MovementGenerator* mv)
{
    switch (mv->GetMovementGeneratorType())
    {
        case CONFUSED_MOTION_TYPE:
        case FLEEING_MOTION_TYPE:
        case TIMED_FLEEING_MOTION_TYPE:
            return Helm::Rank::Panic;

        case CHASE_MOTION_TYPE:
        case FOLLOW_MOTION_TYPE:
            return Helm::Rank::Combat;

        case POINT_MOTION_TYPE:
        case ASSISTANCE_MOTION_TYPE:
        case ASSISTANCE_DISTRACT_MOTION_TYPE:
        case HOME_MOTION_TYPE:
        case EFFECT_MOTION_TYPE:
        case FLIGHT_MOTION_TYPE:
        case DISTRACT_MOTION_TYPE:
            return Helm::Rank::Errand;

        // Idle, wander and the waypoint patrol: what the unit does when nothing else
        // is happening. The default sits at the bottom and is only ever covered.
        default:
            return Helm::Rank::Routine;
    }
}

/**
 * @brief Initializes the MotionMaster.
 */
void MotionMaster::Initialize()
{
    // Stop current move
    m_owner->StopMoving();

    // Clear ALL movement generators (including default)
    Clear(false, true);

    // Set new default movement generator
    if (m_owner->GetTypeId() == TYPEID_UNIT && !m_owner->hasUnitState(UNIT_STAT_CONTROLLED))
    {
        MovementGenerator* movement = FactorySelector::selectMovementGenerator((Creature*)m_owner);
        MovementGenerator* const first =
            (movement == nullptr) ? &si_idleMovement : movement;
        m_roster.Add(first, rankOf(first));
        top()->Initialize(*m_owner);
        if (top()->GetMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            static_cast<WaypointMovementGenerator*>(top())->InitializeWaypointPath(*m_owner, 0, PATH_NO_PATH, 0, 0);
        }
    }
    else
    {
        m_roster.Add(&si_idleMovement, Helm::Rank::Routine);
    }
}

/**
 * @brief Destructor for MotionMaster.
 */
MotionMaster::~MotionMaster()
{
    // Deallocate, but do not Finalize: the owner is already being torn down and a
    // generator's cleanup would reach into memory that has gone.
    for (MovementGenerator* gen : m_roster)
    {
        if (!isStatic(gen))
        {
            delete gen;
        }
    }
    m_roster.Abandon();
}

void MotionMaster::Dispose()
{
    for (MovementGenerator* gen : m_roster.TakeRetired())
    {
        // The idle generator is a shared static, and deleting it would take every
        // other unit's default behaviour with it.
        if (!isStatic(gen))
        {
            delete gen;
        }
    }
}

/**
 * @brief Updates the motion of the unit.
 * @param diff Time difference.
 */
void MotionMaster::UpdateMotion(uint32 diff)
{
    if (m_owner->hasUnitState(UNIT_STAT_CAN_NOT_MOVE))
    {
        return;
    }

    MANGOS_ASSERT(!m_roster.Empty());

    // The driving window is exactly the Update call, and that is the point: a generator
    // that asks to be expired from inside its own Update is asking while we are standing
    // in it, so the removal must not free it yet. One that expires afterwards can be
    // freed at once, and is.
    m_roster.BeginDriving();
    const bool keepDriving = top()->Update(*m_owner, diff);
    m_roster.EndDriving();

    if (!keepDriving)
    {
        MovementExpired();
    }

    if (m_roster.HasRetired())
    {
        Dispose();

        // A unit always has something driving it. Emptying the roster is legal on the
        // way through -- the targeted-motion sweep below can do it -- but never a
        // resting state.
        if (m_roster.Empty())
        {
            Initialize();
        }

        if (m_resetPending)
        {
            m_resetPending = false;
            top()->Reset(*m_owner);
        }
    }
}

void MotionMaster::Clear(bool reset, bool all)
{
    // The floor says the rule once: a unit keeps its default behaviour unless the
    // caller is clearing everything. This was `size() > 1` in four places.
    const std::size_t floor = all ? 0u : 1u;

    while (m_roster.Size() > floor)
    {
        MovementGenerator* gen = top();
        m_roster.RemoveActive(floor);
        gen->Finalize(*m_owner);
    }

    // Called from inside a generator's Update: the retired list holds something we are
    // standing in, so freeing waits and so does the reset. UpdateMotion does both.
    if (m_roster.Driving())
    {
        m_resetPending = reset;
        return;
    }

    Dispose();

    if (!all && reset)
    {
        MANGOS_ASSERT(!m_roster.Empty());
        top()->Reset(*m_owner);
    }
}

void MotionMaster::MovementExpired(bool reset)
{
    // Nothing to expire down to. The default behaviour at the bottom outlives every
    // generator stacked on it.
    if (m_roster.Size() <= 1)
    {
        return;
    }

    MovementGenerator* expiring = top();
    m_roster.RemoveActive(0);

    // ...and the targeted motions parked underneath it go too. No floor here, and that
    // is deliberate: if the sweep empties the roster, Initialize below puts the default
    // back. Stopping at the floor instead would leave a chase running that the caller
    // has just cancelled.
    while (!m_roster.Empty() && isTargeted(top()))
    {
        MovementGenerator* beneath = top();
        m_roster.RemoveActive(0);
        beneath->Finalize(*m_owner);
    }

    // Read BEFORE the finalize, because a generator's cleanup is allowed to push its
    // successor -- a creature that stops fleeing goes home, and says so from inside the
    // flee's own cleanup. Resetting afterwards would reset the newcomer.
    MovementGenerator* const wasTop = m_roster.Empty() ? nullptr : top();
    expiring->Finalize(*m_owner);

    if (m_roster.Driving())
    {
        m_resetPending = reset;
        return;
    }

    Dispose();

    if (m_roster.Empty())
    {
        Initialize();
    }

    if (reset && top() == wasTop)
    {
        top()->Reset(*m_owner);
    }
}

/**
 * @brief Moves the unit to idle state.
 */
void MotionMaster::MoveIdle()
{
    // === "STOP AND DO NOTHING", which is what every caller means by it.
    //
    // Adding idle at Routine and hoping is not that. Ranks decide what drives, and
    // Routine is the bottom of them -- so a chase (Combat) or a flee (Panic) went
    // straight on running while the pet was told to Stay, the guard was told to hold,
    // the script was told to stop. Under the old stack idle became the top and the
    // contract was "stop"; the move to ranks changed it silently, and the callers --
    // PetAI, GuardAI, CreatureEventAI, TransportMap, aura control, scripts -- were not
    // changed with it.
    //
    // So it clears, like the death path already does by hand, and stops the mover.
    // Leaving the spline running was the other half: even where idle DID take over, the
    // generator underneath was never interrupted and the unit kept walking out the rest
    // of its leg, arriving nowhere anyone had asked for and firing no MovementInform.
    if (m_roster.Size() == 1 && isStatic(top()))
    {
        return;   // already idle, and nothing to interrupt
    }

    m_owner->StopMoving();
    Clear(false, true);
    m_roster.Add(&si_idleMovement, Helm::Rank::Routine);
}

/**
 * @brief Moves the unit randomly around a point.
 * @param x X-coordinate of the center point.
 * @param y Y-coordinate of the center point.
 * @param z Z-coordinate of the center point.
 * @param radius Radius of the random movement.
 * @param verticalZ Vertical offset for the movement.
 */
void MotionMaster::MoveRandomAroundPoint(float x, float y, float z, float radius, float /*verticalZ*/)
{
    if (m_owner->GetTypeId() == TYPEID_PLAYER)
    {
        sLog.outError("%s attempt to move random.", m_owner->GetGuidStr().c_str());
    }
    else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s move random.", m_owner->GetGuidStr().c_str());
        Mutate(new RandomMovementGenerator(x, y, z, radius));
    }
}

/**
 * @brief Moves the unit to its home position.
 */
void MotionMaster::MoveTargetedHome()
{
    if (m_owner->hasUnitState(UNIT_STAT_LOST_CONTROL))
    {
        return;
    }

    Clear(false);

    if (m_owner->GetTypeId() == TYPEID_UNIT && !((Creature*)m_owner)->GetCharmerOrOwnerGuid())
    {
        // Manual exception for linked mobs
        if (m_owner->IsLinkingEventTrigger() && m_owner->GetMap()->GetCreatureLinkingHolder()->TryFollowMaster((Creature*)m_owner))
        {
            DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s refollowed linked master", m_owner->GetGuidStr().c_str());
        }
        else
        {
            DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s targeted home", m_owner->GetGuidStr().c_str());
            Mutate(new HomeMovementGenerator());
        }
    }
    else if (m_owner->GetTypeId() == TYPEID_UNIT && ((Creature*)m_owner)->GetCharmerOrOwnerGuid())
    {
        if (Unit* target = ((Creature*)m_owner)->GetCharmerOrOwner())
        {
            DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s follow to %s", m_owner->GetGuidStr().c_str(), target->GetGuidStr().c_str());
            Mutate(new FollowMovementGenerator(*target, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE));
        }
        else
        {
            DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s attempt but fail to follow owner", m_owner->GetGuidStr().c_str());
        }
    }
    else
    {
        sLog.outError("%s attempt targeted home", m_owner->GetGuidStr().c_str());
    }
}

/**
 * @brief Makes the unit move in a confused manner.
 */
void MotionMaster::MoveConfused()
{
    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s move confused", m_owner->GetGuidStr().c_str());

    Mutate(new ConfusedMovementGenerator());
}

/**
 * @brief Makes the unit chase a target.
 * @param target Pointer to the target unit.
 * @param dist Distance to maintain from the target.
 * @param angle Angle to maintain from the target.
 */
void MotionMaster::MoveChase(Unit* target, float dist, float angle)
{
    // Ignore movement request if target not exist
    if (!target)
    {
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s chase to %s", m_owner->GetGuidStr().c_str(), target->GetGuidStr().c_str());

    Mutate(new ChaseMovementGenerator(*target, dist, angle));
}

/**
 * @brief Makes the unit follow a target.
 * @param target Pointer to the target unit.
 * @param dist Distance to maintain from the target.
 * @param angle Angle to maintain from the target.
 */
void MotionMaster::MoveFollow(Unit* target, float dist, float angle)
{
    if (m_owner->hasUnitState(UNIT_STAT_LOST_CONTROL))
    {
        return;
    }

    Clear();

    // Ignore movement request if target not exist
    if (!target)
    {
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s follow to %s", m_owner->GetGuidStr().c_str(), target->GetGuidStr().c_str());

    Mutate(new FollowMovementGenerator(*target, dist, angle));
}

/**
 * @brief Moves the unit to a specific point.
 * @param id ID of the movement.
 * @param x X-coordinate of the destination.
 * @param y Y-coordinate of the destination.
 * @param z Z-coordinate of the destination.
 * @param generatePath Whether to generate a path to the destination.
 */
void MotionMaster::MovePoint(uint32 id, float x, float y, float z, bool generatePath)
{
    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s targeted point (Id: %u X: %f Y: %f Z: %f)", m_owner->GetGuidStr().c_str(), id, x, y, z);

    Mutate(new PointMovementGenerator(id, x, y, z, generatePath));
}

/**
 * @brief Makes the unit seek assistance at a specific point.
 * @param x X-coordinate of the assistance point.
 * @param y Y-coordinate of the assistance point.
 * @param z Z-coordinate of the assistance point.
 */
void MotionMaster::MoveSeekAssistance(float x, float y, float z)
{
    if (m_owner->GetTypeId() == TYPEID_PLAYER)
    {
        sLog.outError("%s attempt to seek assistance", m_owner->GetGuidStr().c_str());
    }
    else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s seek assistance (X: %f Y: %f Z: %f)",
                         m_owner->GetGuidStr().c_str(), x, y, z);
        Mutate(new AssistanceMovementGenerator(x, y, z));
    }
}

/**
 * @brief Makes the unit seek assistance and then distract.
 * @param timer Time for the distraction.
 */
void MotionMaster::MoveSeekAssistanceDistract(uint32 time)
{
    if (m_owner->GetTypeId() == TYPEID_PLAYER)
    {
        sLog.outError("%s attempt to call distract after assistance", m_owner->GetGuidStr().c_str());
    }
    else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s is distracted after assistance call (Time: %u)",
                         m_owner->GetGuidStr().c_str(), time);
        Mutate(new AssistanceDistractMovementGenerator(time));
    }
}

/**
 * @brief Makes the unit flee from an enemy.
 * @param enemy Pointer to the enemy unit.
 * @param time Time limit for the fleeing movement.
 */
void MotionMaster::MoveFleeing(Unit* enemy, uint32 time)
{
    if (!enemy)
    {
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s flee from %s", m_owner->GetGuidStr().c_str(), enemy->GetGuidStr().c_str());

    // Only a creature ever flees on a timer and then turns to fight again; a feared
    // player runs until the aura that frightened it is gone.
    if (time && m_owner->GetTypeId() == TYPEID_UNIT)
    {
        Mutate(new TimedFleeingMovementGenerator(enemy->GetObjectGuid(), time));
    }
    else
    {
        Mutate(new FleeingMovementGenerator(enemy->GetObjectGuid()));
    }
}

/**
 * @brief Moves the unit along a waypoint path.
 * @param id ID of the waypoint path.
 * @param source Source of the waypoint path.
 * @param initialDelay Initial delay before starting the movement.
 * @param overwriteEntry Entry to overwrite.
 */
void MotionMaster::MoveWaypoint(int32 id /*=0*/, uint32 source /*=0==PATH_NO_PATH*/, uint32 initialDelay /*=0*/, uint32 overwriteEntry /*=0*/)
{
    if (m_owner->GetTypeId() == TYPEID_UNIT)
    {
        if (GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            sLog.outError("%s attempt to MoveWaypoint() but is already using waypoint", m_owner->GetGuidStr().c_str());
            return;
        }

        Creature* creature = (Creature*)m_owner;

        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s start MoveWaypoint()", m_owner->GetGuidStr().c_str());
        WaypointMovementGenerator* newWPMMgen = new WaypointMovementGenerator(*creature);
        Mutate(newWPMMgen);
        newWPMMgen->InitializeWaypointPath(*creature, id, (WaypointPathOrigin)source, initialDelay, overwriteEntry);
    }
    else
    {
        sLog.outError("Non-creature %s attempt to MoveWaypoint()", m_owner->GetGuidStr().c_str());
    }
}

/**
 * @brief Moves the unit along a taxi flight path.
 * @param path ID of the flight path.
 * @param pathnode Node of the flight path.
 */
void MotionMaster::MoveTaxiFlight(uint32 path, uint32 pathnode)
{
    if (m_owner->GetTypeId() == TYPEID_PLAYER)
    {
        if (path < sTaxiPathNodesByPath.size())
        {
            DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s taxi to (Path %u node %u)", m_owner->GetGuidStr().c_str(), path, pathnode);
            FlightPathMovementGenerator* mgen = new FlightPathMovementGenerator(sTaxiPathNodesByPath[path], pathnode);
            Mutate(mgen);
        }
        else
        {
            sLog.outError("%s attempt taxi to (nonexistent Path %u node %u)",
                          m_owner->GetGuidStr().c_str(), path, pathnode);
        }
    }
    else
    {
        sLog.outError("%s attempt taxi to (Path %u node %u)",
                      m_owner->GetGuidStr().c_str(), path, pathnode);
    }
}

/**
 * @brief Moves the unit along several booked taxi legs as one uninterrupted spline.
 * @param route Concatenated path nodes, already sliced to the starting node.
 * @param junctions Route indices of the hubs the flight passes through without landing.
 */
void MotionMaster::MoveTaxiFlight(TaxiPathNodeList const& route, std::vector<uint32> const& junctions)
{
    if (m_owner->GetTypeId() != TYPEID_PLAYER)
    {
        sLog.outError("%s attempt merged taxi route", m_owner->GetGuidStr().c_str());
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s taxi over %u nodes, %u hubs flown through",
                     m_owner->GetGuidStr().c_str(), uint32(route.size()), uint32(junctions.size()));

    Mutate(new FlightPathMovementGenerator(route, junctions));
}

/**
 * @brief Makes the unit distracted for a specified time.
 * @param timer Time limit for the distraction.
 */
void MotionMaster::MoveDistract(uint32 timer)
{
    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s distracted (timer: %u)", m_owner->GetGuidStr().c_str(), timer);
    DistractMovementGenerator* mgen = new DistractMovementGenerator(timer);
    Mutate(mgen);
}

/**
 * @brief Makes the unit fly or land.
 * @param id ID of the movement.
 * @param x X-coordinate of the destination.
 * @param y Y-coordinate of the destination.
 * @param z Z-coordinate of the destination.
 * @param liftOff Whether the unit should lift off or land.
 */
void MotionMaster::MoveFlyOrLand(uint32 id, float x, float y, float z, bool liftOff)
{
    if (m_owner->GetTypeId() != TYPEID_UNIT)
    {
        return;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "%s targeted point for %s (Id: %u X: %f Y: %f Z: %f)", m_owner->GetGuidStr().c_str(), liftOff ? "liftoff" : "landing", id, x, y, z);
    Mutate(new FlyOrLandMovementGenerator(id, x, y, z, liftOff));
}

/**
 * @brief Changes the current movement generator to a new one.
 * @param m Pointer to the new movement generator.
 */
void MotionMaster::Mutate(MovementGenerator* m)
{
    if (!empty())
    {
        switch (top()->GetMovementGeneratorType())
        {
                // HomeMovement is not that important, delete it if meanwhile a new comes
            case HOME_MOTION_TYPE:
                // DistractMovement interrupted by any other movement
            case DISTRACT_MOTION_TYPE:
                MovementExpired(false);
            default:
                break;
        }
    }

    // Who was driving BEFORE the new generator joined. Recorded rather than
    // interrupted on the spot, because whether it is still driving afterwards is not
    // this function's to assume any more.
    MovementGenerator* previous = empty() ? nullptr : top();

    // === INITIALISE ONLY WHAT ACTUALLY TAKES THE WHEEL.
    //
    // `Initialize` is not a constructor. PointMovementGenerator's calls StopMoving() and
    // sets UNIT_STAT_ROAMING; Random's sets it too. Running that for a generator that
    // then joins BELOW the driver killed the driver's spline and cleared its
    // UNIT_STAT_CHASE_MOVE -- so a script's MovePoint during a chase stopped the chase
    // dead, never drove, and the next chase tick had to lay the leg again. A visible
    // hitch, caused by a generator that never got to do anything.
    //
    // Two generators still need their init BEFORE they are ranked, and for one reason:
    // they capture where the unit is now. Home reads its anchor from the generator it is
    // displacing (GetResetPosition), and an effect reads the leg it was launched with.
    // Both are captures, neither steers, so both are safe here.
    const MovementGeneratorType kind = m->GetMovementGeneratorType();
    const bool capturesOnInit =
        kind == HOME_MOTION_TYPE || kind == EFFECT_MOTION_TYPE;

    if (capturesOnInit)
    {
        m->Initialize(*m_owner);
    }

    m_roster.Add(m, rankOf(m));

    if (!capturesOnInit && top() == m)
    {
        m->Initialize(*m_owner);
    }

    // Interrupt the outgoing driver ONLY if it really is outgoing.
    //
    // This used to interrupt the top of the roster unconditionally, before adding --
    // which was right when the roster was a stack and the newest entry always took
    // over. It stopped being right when Active() started picking by RANK: a MovePoint
    // (Errand) issued during a chase (Combat) interrupted the chase, joined below it,
    // and did not take the wheel. The next tick was then driven by a chase sitting in
    // Interrupt state -- covered, paused, and still steering the unit.
    //
    // Note what this does NOT decide: whether the lower-ranked newcomer should have
    // preempted at all. Under the old stack it would have; under ranks it does not,
    // and that is the ranking's whole purpose ("the most important generator drives,
    // not merely the most recent"). Making a script's MovePoint outrank a chase is a
    // decision about the game, not a defect in this function, so it is left visible
    // rather than quietly changed here.
    if (previous && top() != previous)
    {
        previous->Interrupt(*m_owner);
    }
    else if (previous)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS,
                         "%s: %u added but %u still drives (rank did not win)",
                         m_owner->GetGuidStr().c_str(),
                         uint32(m->GetMovementGeneratorType()),
                         uint32(previous->GetMovementGeneratorType()));
    }
}

/**
 * @brief Propagates the speed change to the movement generators.
 */
void MotionMaster::PropagateSpeedChange()
{
    for (MovementGenerator* gen : m_roster)
    {
        gen->unitSpeedChanged();
    }
}

/**
 * @brief Sets the next waypoint for the unit.
 * @param pointId ID of the next waypoint.
 * @return True if the next waypoint was successfully set, false otherwise.
 */
bool MotionMaster::SetNextWaypoint(uint32 pointId)
{
    for (auto rItr = m_roster.rbegin(); rItr != m_roster.rend(); ++rItr)
    {
        if ((*rItr)->GetMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            return (static_cast<WaypointMovementGenerator*>(*rItr))->SetNextWaypoint(pointId);
        }
    }
    return false;
}

/**
 * @brief Gets the last reached waypoint.
 * @return The ID of the last reached waypoint.
 */
uint32 MotionMaster::getLastReachedWaypoint() const
{
    for (auto rItr = m_roster.rbegin(); rItr != m_roster.rend(); ++rItr)
    {
        if ((*rItr)->GetMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            return (static_cast<WaypointMovementGenerator*>(*rItr))->getLastReachedWaypoint();
        }
    }
    return 0;
}

/**
 * @brief Gets the type of the current movement generator.
 * @return The type of the current movement generator.
 */
MovementGeneratorType MotionMaster::GetCurrentMovementGeneratorType() const
{
    if (empty())
    {
        return IDLE_MOTION_TYPE;
    }

    return top()->GetMovementGeneratorType();
}

/**
 * @brief Gets the waypoint path information.
 * @param oss Output stream to store the waypoint path information.
 */
void MotionMaster::GetWaypointPathInformation(std::ostringstream& oss) const
{
    for (auto rItr = m_roster.rbegin(); rItr != m_roster.rend(); ++rItr)
    {
        if ((*rItr)->GetMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            static_cast<WaypointMovementGenerator*>(*rItr)->GetPathInformation(oss);
            return;
        }
    }
}

/**
 * @brief Gets the destination coordinates.
 * @param x Reference to the X-coordinate.
 * @param y Reference to the Y-coordinate.
 * @param z Reference to the Z-coordinate.
 * @return True if the destination coordinates were successfully obtained, false otherwise.
 */
bool MotionMaster::GetDestination(float& x, float& y, float& z)
{
    if (!m_owner->IsTravelling())
    {
        return false;
    }

    const Geometry::Vector3& dest = m_owner->CurrentCourse().Points().back();
    x = dest.x;
    y = dest.y;
    z = dest.z;
    return true;
}

/**
 * @brief Makes the unit fall to the ground.
 */
void MotionMaster::MoveFall()
{
    // Use larger distance for vmap height search than in most other cases
    float tz = m_owner->GetMap()->GetHeight(m_owner->Where().X(), m_owner->Where().Y(), m_owner->Where().Z());
    if (tz <= INVALID_HEIGHT)
    {
        DEBUG_LOG("MotionMaster::MoveFall: unable retrive a proper height at map %u (x: %f, y: %f, z: %f).",
                  m_owner->GetMap()->GetId(), m_owner->Where().X(), m_owner->Where().Y(), m_owner->Where().Z());
        return;
    }

    // Abort too if the ground is very near
    if (fabs(m_owner->Where().Z() - tz) < 0.1f)
    {
        return;
    }

    Movement::MoveSplineInit init(*m_owner);
    init.MoveTo(m_owner->Where().X(), m_owner->Where().Y(), tz);
    init.SetFall();
    init.Launch();
    Mutate(new EffectMovementGenerator(0));
}
