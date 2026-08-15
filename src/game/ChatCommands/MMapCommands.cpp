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

/**
 * @file MMapCommands.cpp
 * @brief Implementation of movement map and pathfinding chat commands.
 *
 * This file contains chat command handlers for MMap operations including:
 * - Movement map testing and validation
 * - Pathfinding debugging
 * - Path generation and verification
 */

#include "Chat.h"
#include "ObjectMgr.h"
#include "World.h"
#include "MotionGenerators/Pathing.h"
#include "nav/NavStore.hpp"
#include "nav/NavTileIO.hpp"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"          // for mmap manager
#include "CellImpl.h"
#include "movement/MoveSplineInit.h"
#include "GameTime.h"
#include <fstream>
#include <map>
#include <typeinfo>
#include <cstring>
#include <list>

namespace
{
    // Switches rather than a table indexed by the enum. A table is one value away from
    // reading past its own end -- add a RouteStop and every command that prints one
    // starts quoting whatever follows the array -- while a switch with no default makes
    // the omission a compiler warning on both GCC and Clang, which is where it belongs.

    const char* RouteOutcomeName(Nav::RouteOutcome outcome)
    {
        switch (outcome)
        {
            case Nav::RouteOutcome::Routed:     return "routed";
            case Nav::RouteOutcome::Partial:    return "partial";
            case Nav::RouteOutcome::Direct:     return "direct (no routing)";
            case Nav::RouteOutcome::Unroutable: return "unroutable";
        }
        return "unknown";
    }

    const char* RouteStopName(Nav::RouteStop stop)
    {
        switch (stop)
        {
            case Nav::RouteStop::Reached:    return "reached the goal";
            case Nav::RouteStop::Wall:       return "the world blocked it";
            case Nav::RouteStop::NodeBudget: return "the fine search ran out of cells";
            case Nav::RouteStop::PointBudget: return "the point budget was spent";
            case Nav::RouteStop::LengthBudget: return "the allowed length was spent";
            case Nav::RouteStop::NoMesh:     return "no navigation here";
            case Nav::RouteStop::OffMesh:    return "start or goal is off the ground";
            case Nav::RouteStop::TooNarrow:  return "the mover is too wide for the way";
            case Nav::RouteStop::Forced:     return "destination was forced";
            case Nav::RouteStop::Failed:     return "the query failed";
        }
        return "unknown";
    }
}

/**
 * @brief Handler for HandleMmapPathCommand command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmapPathCommand(char* args)
{
    PSendSysMessage("mmap path:");

    // units
    Player* player = m_session->GetPlayer();
    Unit* target = getSelectedUnit();
    if (!player || !target)
    {
        PSendSysMessage("Invalid target/source selection.");
        return true;
    }

    char* para = strtok(args, " ");

    bool followPath = false;
    bool unitToPlayer = false;
    if (para)
    {
        if (strcmp(para, "go") == 0)
        {
            followPath = true;
        }
        else if (strcmp(para, "to_me") == 0)
        {
            unitToPlayer = true;
        }
        else
        {
            PSendSysMessage("Use '.mmap path go' to move on target.");
            PSendSysMessage("Use '.mmap path to_me' to generate path from the target to you.");
        }
    }

    Unit* destinationUnit;
    Unit* originUnit;
    if (unitToPlayer)
    {
        destinationUnit = player;
        originUnit = target;
    }
    else
    {
        destinationUnit = target;
        originUnit = player;
    }

    // unit locations
    float x, y, z;
    x = destinationUnit->Where().X();
    y = destinationUnit->Where().Y();
    z = destinationUnit->Where().Z();

    // path
    Pathing path(originUnit);
    path.calculate(x, y, z);

    const Movement::PointsArray pointPath = path.getPath();

    const Nav::Route& route = path.getRoute();
    PSendSysMessage("%s's path to %s:", originUnit->GetName(), destinationUnit->GetName());
    PSendSysMessage("length %zu, %s, stopped: %s", pointPath.size(),
                    RouteOutcomeName(route.outcome), RouteStopName(route.stop));

    const Geometry::Vector3 start = path.getStartPosition();
    const Geometry::Vector3 end = path.getEndPosition();
    const Geometry::Vector3 actualEnd = path.getActualEndPosition();

    PSendSysMessage("start      (%.3f, %.3f, %.3f)", start.x, start.y, start.z);
    PSendSysMessage("end        (%.3f, %.3f, %.3f)", end.x, end.y, end.z);
    PSendSysMessage("actual end (%.3f, %.3f, %.3f)", actualEnd.x, actualEnd.y, actualEnd.z);

    if (!player->isGameMaster())
    {
        PSendSysMessage("Enable GM mode to see the path points.");
    }

    for (uint32 i = 0; i < pointPath.size(); ++i)
    {
        player->SummonCreature(VISUAL_WAYPOINT, pointPath[i].x, pointPath[i].y, pointPath[i].z, 0, TEMPSPAWN_TIMED_DESPAWN, 9000);
    }

    if (followPath)
    {
        Movement::MoveSplineInit init(*player);
        init.MovebyPath(pointPath);
        init.SetWalk(false);
        init.Launch();
    }

    return true;
}

/**
 * @brief Handler for HandleMmapLocCommand command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmapLocCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();

    const float x = player->Where().X();
    const float y = player->Where().Y();
    const float z = player->Where().Z();

    const Nav::CellRef cell = Nav::CellAt(x, y);

    PSendSysMessage("nav loc:");
    PSendSysMessage("file  %s", Nav::NavTileFileName(player->GetMapId(), cell.TileX(),
                                                     cell.TileY()).c_str());
    PSendSysMessage("tile  [%i,%i]  cell [%i,%i] of %i",
                    cell.TileX(), cell.TileY(), cell.LocalX(), cell.LocalY(),
                    Nav::CELLS_PER_TILE);

    const Nav::NavStore* store = Nav::NavStores::Instance().Find(player->GetMapId());
    if (!store)
    {
        PSendSysMessage("No navigation loaded for this map.");
        return true;
    }

    const std::shared_ptr<const Nav::NavTile> tile = store->TileOf(cell);
    if (!tile)
    {
        PSendSysMessage("That tile is not resident.");
        return true;
    }

    // Every surface under the point, not just the one selected. Which surface a
    // question means is the caller's choice, and a command whose whole job is to say
    // what the data holds should show all of them.
    std::vector<Nav::Surface> surfaces;
    tile->SurfacesAt(cell.InTile(), surfaces);

    if (surfaces.empty())
    {
        PSendSysMessage("No walkable surface in this cell.");
        return true;
    }

    for (size_t i = 0; i < surfaces.size(); ++i)
    {
        const Nav::Surface& surface = surfaces[i];
        PSendSysMessage("  layer %u  z %.3f (you: %.3f)  area %u  region %u  room %.2f yd",
                        uint32(i), surface.z, z, uint32(Nav::AreaOf(surface.area)),
                        uint32(surface.region),
                        Nav::RestoreClearance(surface.clearance));
    }

    return true;
}

/**
 * @brief Handler for HandleMmapLoadedTilesCommand command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmapLoadedTilesCommand(char* /*args*/)
{
    const uint32 mapid = m_session->GetPlayer()->GetMapId();

    const Nav::NavStore* store = Nav::NavStores::Instance().Find(mapid);
    if (!store || store->ResidentCount() == 0)
    {
        PSendSysMessage("No navigation loaded for this map.");
        return true;
    }

    std::vector<Nav::TileKey> resident;
    store->ResidentTiles(resident);

    PSendSysMessage("nav loadedtiles:");
    for (std::vector<Nav::TileKey>::const_iterator it = resident.begin();
         it != resident.end(); ++it)
    {
        const std::shared_ptr<const Nav::NavTile> tile =
            store->TileAt(it->x, it->y);
        PSendSysMessage("[%02i,%02i]  %u regions, %u gateways", int(it->x), int(it->y),
                        tile ? uint32(tile->Regions().size()) : 0,
                        tile ? uint32(tile->Gateways().size()) : 0);
    }

    return true;
}

/**
 * @brief Handler for HandleMmapStatsCommand command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmapStatsCommand(char* /*args*/)
{
    PSendSysMessage("nav stats:");
    PSendSysMessage("  global pathfinding is %sabled",
                    sWorld.getConfig(CONFIG_BOOL_MMAP_ENABLED) ? "en" : "dis");

    const Nav::NavStores& stores = Nav::NavStores::Instance();
    PSendSysMessage("  %zu maps loaded with %zu tiles overall", stores.MapCount(),
                    stores.TileCount());

    const Nav::NavStore* store =
        stores.Find(m_session->GetPlayer()->GetMapId());
    if (!store || store->ResidentCount() == 0)
    {
        PSendSysMessage("No navigation loaded for this map.");
        return true;
    }

    std::vector<Nav::TileKey> resident;
    store->ResidentTiles(resident);

    uint32 regions = 0;
    uint32 gateways = 0;
    uint32 stacked = 0;
    for (std::vector<Nav::TileKey>::const_iterator it = resident.begin();
         it != resident.end(); ++it)
    {
        if (const std::shared_ptr<const Nav::NavTile> tile =
                store->TileAt(it->x, it->y))
        {
            regions += uint32(tile->Regions().size());
            gateways += uint32(tile->Gateways().size());
            stacked += uint32(tile->Stacked().size());
        }
    }

    PSendSysMessage("Navigation on current map:");
    PSendSysMessage("  %zu tiles resident", store->ResidentCount());
    PSendSysMessage("  %u regions, %u gateways", regions, gateways);
    PSendSysMessage("  %u stacked surfaces", stacked);
    PSendSysMessage("  %.2f MB resident", float(store->Footprint()) / 1048576.0f);

    return true;
}

/**
 * @brief Handler for HandleMmap command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmap(char* args)
{
    bool on;
    if (ExtractOnOff(&args, on))
    {
        if (on)
        {
            sWorld.setConfig(CONFIG_BOOL_MMAP_ENABLED, true);
            SendSysMessage("WORLD: mmaps are now ENABLED (individual map settings still in effect)");
        }
        else
        {
            sWorld.setConfig(CONFIG_BOOL_MMAP_ENABLED, false);
            SendSysMessage("WORLD: mmaps are now DISABLED");
        }
        return true;
    }

    on = sWorld.getConfig(CONFIG_BOOL_MMAP_ENABLED);
    PSendSysMessage("mmaps are %sabled", on ? "en" : "dis");

    return true;
}

/**
 * @brief Handler for HandleMmapTestArea command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmapTestArea(char* args)
{
    float radius = 40.0f;
    ExtractFloat(&args, radius);

    std::list<Creature*> creatureList;
    MaNGOS::AnyUnitInObjectRangeCheck go_check(m_session->GetPlayer(), radius);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> go_search(creatureList, go_check);
    // Get Creatures
    Cell::VisitGridObjects(m_session->GetPlayer(), go_search, radius);

    if (!creatureList.empty())
    {
        PSendSysMessage("Found %zu Creatures.", creatureList.size());

        uint32 paths = 0;
        uint32 uStartTime = GameTime::GetGameTimeMS();

        float gx, gy, gz;
        gx = m_session->GetPlayer()->Where().X();
        gy = m_session->GetPlayer()->Where().Y();
        gz = m_session->GetPlayer()->Where().Z();
        for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
        {
            Pathing path(*itr);
            path.calculate(gx, gy, gz);
            ++paths;
        }

        uint32 uPathLoadTime = getMSTimeDiff(uStartTime, GameTime::GetGameTimeMS());
        PSendSysMessage("Generated %i paths in %i ms", paths, uPathLoadTime);
    }
    else
    {
        PSendSysMessage("No creatures in %f yard range.", radius);
    }

    return true;
}

/**
 * @brief Handler for HandleMmapTestHeight command.
 *
 * @param args Command arguments.
 * @returns True if the command executed successfully, false otherwise.
 */
bool ChatHandler::HandleMmapTestHeight(char* args)
{
    float radius = 0.0f;
    ExtractFloat(&args, radius);
    if (radius > 40.0f)
    {
        radius = 40.0f;
    }

    Unit* unit = getSelectedUnit();

    Player* player = m_session->GetPlayer();
    if (!unit)
    {
        unit = player;
    }

    if (unit->GetTypeId() == TYPEID_UNIT)
    {
        if (radius < 0.1f)
        {
            radius = static_cast<Creature*>(unit)->GetRespawnRadius();
        }
    }
    else
    {
        if (unit->GetTypeId() != TYPEID_PLAYER)
        {
            PSendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
            return false;
        }
    }

    if (radius < 0.1f)
    {
        PSendSysMessage("Provided spawn radius for %s is too small. Using 5.0f instead.", unit->GetGuidStr().c_str());
        radius = 5.0f;
    }

    float gx, gy, gz;
    gx = unit->Where().X();
    gy = unit->Where().Y();
    gz = unit->Where().Z();

    Creature* summoned = unit->SummonCreature(VISUAL_WAYPOINT, gx, gy, gz + 0.5f, 0, TEMPSPAWN_TIMED_DESPAWN, 20000);
    summoned->CastSpell(summoned, 8599, false);
    uint32 tries = 1;
    uint32 successes = 0;
    uint32 startTime = GameTime::GetGameTimeMS();
    for (; tries < 500; ++tries)
    {
        gx = unit->Where().X();
        gy = unit->Where().Y();
        gz = unit->Where().Z();
        if (unit->GetMap()->GetReachableRandomPosition(unit, gx, gy, gz, radius))
        {
            unit->SummonCreature(VISUAL_WAYPOINT, gx, gy, gz, 0, TEMPSPAWN_TIMED_DESPAWN, 15000);
            ++successes;
            if (successes >= 100)
            {
                break;
            }
        }
    }
    uint32 genTime = getMSTimeDiff(startTime, GameTime::GetGameTimeMS());
    PSendSysMessage("Generated %u valid points for %u try in %ums.", successes, tries, genTime);
    return true;
}
