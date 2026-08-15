# SPDX-License-Identifier: GPL-3.0-or-later
#
# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

#AH Bot group
file(GLOB SRC_GRP_AHBOT AuctionHouseBot/*.cpp AuctionHouseBot/*.h)
source_group("AhBot" FILES ${SRC_GRP_AHBOT})

#Battlegrounds group
file(GLOB SRC_GRP_BATTLEGROUND BattleGround/*.cpp BattleGround/*.h)
source_group("BattleGround" FILES ${SRC_GRP_BATTLEGROUND})

#Chat Commands group
file(GLOB SRC_GRP_CHAT_COMMANDS ChatCommands/*.cpp ChatCommands/*.h)
source_group("Chat Commands" FILES ${SRC_GRP_CHAT_COMMANDS})

#Map Threading group
#Combat group -- the bridge only. The rules themselves are in combat/pure,
#which is its own library and must stay out of this glob: it links into the
#unit tests without the game library, and a file that reaches back into the
#server would end that.
file(GLOB SRC_GRP_COMBAT combat/*.cpp combat/*.h)
source_group("Combat" FILES ${SRC_GRP_COMBAT})

file(GLOB SRC_GRP_MAP_THREAD Maps/*.cpp Maps/*.h)
source_group("Map Threading" FILES ${SRC_GRP_MAP_THREAD})

#Motion Generator group
file(GLOB SRC_GRP_MOTION_GEN MotionGenerators/*.cpp MotionGenerators/*.h)
source_group("Motion generators" FILES ${SRC_GRP_MOTION_GEN})

#Movement group
file(GLOB SRC_GRP_MOVEMENT movement/*.cpp movement/*.h)
source_group("Movement" FILES ${SRC_GRP_MOVEMENT})

# Motion group -- the course model. Separate from "Movement" (the spline machinery it
# will replace) on purpose: these files depend on points, a clock and a guid, and on
# nothing in the server, which is what lets the unit tests compile them directly.
file(GLOB SRC_GRP_MOTION motion/*.cpp motion/*.h)
source_group("Motion" FILES ${SRC_GRP_MOTION})

#Object group
file(GLOB SRC_GRP_OBJECT Object/*.cpp Object/*.h)
source_group("Object" FILES ${SRC_GRP_OBJECT})

#Outdoors PvP group
file(GLOB SRC_GRP_OUTDOOR_PVP OutdoorPvP/*.cpp OutdoorPvP/*.h)
source_group("Outdoor PvP" FILES ${SRC_GRP_OUTDOOR_PVP})

#References group
file(GLOB SRC_GRP_REFERENCES References/*.cpp References/*.h)
source_group("References" FILES ${SRC_GRP_REFERENCES})

#Scripting seam group
# Always built, with or without an engine: the seam is what the world talks to,
# and with no engine compiled in every emit site folds to a single test.
file(GLOB SRC_GRP_SCRIPTING Scripting/*.cpp Scripting/*.h)
source_group("Scripting" FILES ${SRC_GRP_SCRIPTING})

# One directory per engine under Scripting/. Adding an engine is a new
# directory plus one line in ScriptHost.cpp's MakeHostState -- nothing here
# needs editing, and nothing outside Scripting/ ever names an engine.
#
# GLOB_RECURSE, because an engine's directory is its whole nest: the adapter
# that implements IEngine sits at the top and whatever it drives lives under
# it, at whatever depth suits it. SD3 keeps 480 scripts sorted by continent and
# instance under sd3/engine/, and the build follows the layout instead of
# listing it -- the file that used to do the listing was 32 KB of one glob per
# dungeon, and every new script directory was a build edit waiting to be
# forgotten.
file(GLOB_RECURSE SRC_GRP_SCRIPT_ENGINES Scripting/*/*.cpp Scripting/*/*.h)

# An engine that can be configured out is dropped whole. The adapter beside it
# still compiles -- it guards its own body -- so the seam stays the same shape
# with the back end absent, which is the point of a seam.
if(NOT SCRIPT_LIB_SD3)
    list(FILTER SRC_GRP_SCRIPT_ENGINES EXCLUDE REGEX "/Scripting/sd3/engine/")
endif()

# Luau is dropped whole too, and "whole" has to mean the API layer as well as
# the VM. Excluding only engine/ left Scripting/luau/api/*.cpp in the build
# while the Luau include roots below were NOT added, so five of those files
# failed on `#include "lua.h"` and configuring the engine off broke the build
# outright. LuauEngine.cpp is the one file that stays: it guards its own body
# with ENABLE_LUAU and compiles to nothing, which is what keeps the seam the
# same shape with the back end absent.
if(NOT SCRIPT_LIB_LUAU)
    list(FILTER SRC_GRP_SCRIPT_ENGINES EXCLUDE REGEX "/Scripting/luau/engine/")
    list(FILTER SRC_GRP_SCRIPT_ENGINES EXCLUDE REGEX "/Scripting/luau/api/")
endif()

# The vendored Luau VM is third-party C++ that knows nothing about this core,
# so it must not be handed the core's precompiled header: /Yu forces pchdef.h
# into every source of the target, and these sources have their own idea of
# what a uint32 is. They are excluded by name rather than moved out of `game`,
# because being in `game` is what removes the library cycle SD3 used to have.
file(GLOB_RECURSE SRC_GRP_LUAU_VENDORED Scripting/luau/engine/*.cpp)
set_source_files_properties(${SRC_GRP_LUAU_VENDORED} PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON)

list(APPEND SRC_GRP_SCRIPTING ${SRC_GRP_SCRIPT_ENGINES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Scripting" PREFIX "Scripting"
    FILES ${SRC_GRP_SCRIPT_ENGINES})

#Server group
file(GLOB SRC_GRP_SERVER Server/*.cpp Server/*.h)
source_group("Server" FILES ${SRC_GRP_SERVER})

#Time group
file(GLOB SRC_GRP_TIME Time/*.cpp Time/*.h)
source_group("Time" FILES ${SRC_GRP_TIME})

#Tools group
file(GLOB SRC_GRP_TOOL Tools/*.cpp Tools/*.h)
source_group("Tool" FILES ${SRC_GRP_TOOL})



#Warden group
file(GLOB SRC_GRP_WARDEN Warden/*.cpp Warden/*.h)
source_group("Warden" FILES ${SRC_GRP_WARDEN})

#Warden Modules group
file(GLOB SRC_GRP_WARDEN_MODULES Warden/Modules/*.cpp Warden/Modules/*.h)
source_group("Warden\\Modules" FILES ${SRC_GRP_WARDEN_MODULES})

#World and Handlers group
file(GLOB SRC_GRP_WORLD_HANDLERS WorldHandlers/*.cpp WorldHandlers/*.h)
source_group("World\\Handlers" FILES ${SRC_GRP_WORLD_HANDLERS})

if(PLAYERBOTS)

#Base files
file(GLOB Playerbot_Source ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/*.h)
source_group("Player Bot" FILES ${Playerbot_Source})
file(GLOB AHbot_Source ${CMAKE_SOURCE_DIR}/src/modules/Bots/ahbot/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/ahbot/*.h)
source_group("AH Bot" FILES ${AHbot_Source})

set(SRC_GRP_BOTS
    ${Playerbot_Source}
    ${AHbot_Source}
)
#Strategy files
file(GLOB Playerbot_Strategy ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/*.h)
source_group("Player Bot\\Strategies" FILES ${Playerbot_Strategy})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Strategy})

#Action files
file(GLOB Playerbot_Actions ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/actions/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/actions/*.h)
source_group("Player Bot\\Strategies\\Actions" FILES ${Playerbot_Actions})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Actions})

#Generic files
file(GLOB Playerbot_Generic ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/generic/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/generic/*.h)
source_group("Player Bot\\Strategies\\Generic" FILES ${Playerbot_Generic})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Generic})

#Trigger files
file(GLOB Playerbot_Triggers ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/triggers/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/triggers/*.h)
source_group("Player Bot\\Strategies\\Triggers" FILES ${Playerbot_Triggers})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Triggers})


#Value files
file(GLOB Playerbot_Values ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/values/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/values/*.h)
source_group("Player Bot\\Strategies\\Values" FILES ${Playerbot_Values})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Values})

## Class files
#Druid AI
file(GLOB Playerbot_Druid ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/druid/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/druid/*.h)
source_group("Player Bot\\Strategies\\Druid" FILES ${Playerbot_Druid})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Druid})

#Hunter AI
file(GLOB Playerbot_Hunter ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/hunter/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/hunter/*.h)
source_group("Player Bot\\Strategies\\Hunter" FILES ${Playerbot_Hunter})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Hunter})

#Mage AI
file(GLOB Playerbot_Mage ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/mage/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/mage/*.h)
source_group("Player Bot\\Strategies\\Mage" FILES ${Playerbot_Mage})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Mage})

#Paladin AI
file(GLOB Playerbot_Paladin ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/paladin/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/paladin/*.h)
source_group("Player Bot\\Strategies\\Paladin" FILES ${Playerbot_Paladin})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Paladin})

#Priest AI
file(GLOB Playerbot_Priest ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/priest/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/priest/*.h)
source_group("Player Bot\\Strategies\\Priest" FILES ${Playerbot_Priest})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Priest})

#Rogue AI
file(GLOB Playerbot_Rogue ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/rogue/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/rogue/*.h)
source_group("Player Bot\\Strategies\\Rogue" FILES ${Playerbot_Rogue})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Rogue})

#Shaman AI
file(GLOB Playerbot_Shaman ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/shaman/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/shaman/*.h)
source_group("Player Bot\\Strategies\\Shaman" FILES ${Playerbot_Shaman})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Shaman})

#Warlock AI
file(GLOB Playerbot_Warlock ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/warlock/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/warlock/*.h)
source_group("Player Bot\\Strategies\\Warlock" FILES ${Playerbot_Warlock})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Warlock})

#Warrior AI
file(GLOB Playerbot_Warrior ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/warrior/*.cpp ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/strategy/warrior/*.h)
source_group("Player Bot\\Strategies\\Warrior" FILES ${Playerbot_Warrior})
LIST(APPEND SRC_GRP_BOTS ${Playerbot_Warrior})

configure_file(${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot/aiplayerbot.conf.dist.in ${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf.dist)

endif()

configure_file(AuctionHouseBot/ahbot.conf.dist.in ${CMAKE_CURRENT_BINARY_DIR}/AuctionHouseBot/ahbot.conf.dist)


add_library(game STATIC
    ${SRC_GRP_AHBOT}
    ${SRC_GRP_BATTLEGROUND}
    ${SRC_GRP_CHAT_COMMANDS}
    ${SRC_GRP_COMBAT}
    ${SRC_GRP_MAP_THREAD}
    ${SRC_GRP_MOTION_GEN}
    ${SRC_GRP_MOVEMENT}
    ${SRC_GRP_MOTION}
    ${SRC_GRP_OBJECT}
    ${SRC_GRP_OUTDOOR_PVP}
    ${SRC_GRP_REFERENCES}
    ${SRC_GRP_SCRIPTING}
    ${SRC_GRP_SERVER}
    ${SRC_GRP_TIME}
    ${SRC_GRP_TOOL}
    ${SRC_GRP_WARDEN}
    ${SRC_GRP_WARDEN_MODULES}
    ${SRC_GRP_WORLD_HANDLERS}
    $<$<BOOL:${PLAYERBOTS}>:${SRC_GRP_BOTS}>
)

target_include_directories(game
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        AuctionHouseBot
        BattleGround
        ChatCommands
        Maps
        MotionGenerators
        motion
        Object
        OutdoorPvP
        References
        Scripting
        Server
        Time
        Tools
        Warden
        Warden/Modules
        WorldHandlers
        # SD3's own three roots. The scripts include "precompiled.h",
        # "sc_creature.h" and "system/ScriptDevMgr.h" by those names and always
        # have; these are what makes those spellings resolve now that the
        # engine lives in the tree instead of behind a library of its own.
        $<$<BOOL:${SCRIPT_LIB_SD3}>:
            Scripting/sd3/engine
            Scripting/sd3/engine/base
            Scripting/sd3/engine/include
        >
        # Luau ships one include root per component and every header is
        # reached as "Luau/<name>.h" from inside them.
        $<$<BOOL:${SCRIPT_LIB_LUAU}>:
            Scripting/luau/engine/Common/include
            Scripting/luau/engine/Ast/include
            Scripting/luau/engine/Bytecode/include
            Scripting/luau/engine/Compiler/include
            Scripting/luau/engine/VM/include
        >
        $<$<BOOL:${PLAYERBOTS}>:
            ${CMAKE_SOURCE_DIR}/src/modules/Bots
            ${CMAKE_SOURCE_DIR}/src/modules/Bots/playerbot
            ${CMAKE_SOURCE_DIR}/src/modules/Bots/ahbot
        >
)

target_compile_definitions(game
    PUBLIC
        $<$<BOOL:${SOAP}>:ENABLE_SOAP>
        # The old coordinate-owning spatial API. SD3 no longer needs it -- it calls
        # Where()/Place() -- so this is the playerbots module's alone now that
        # Eluna is gone. See Object/ScriptApiCompat.inl.
        $<$<BOOL:${PLAYERBOTS}>:MANGOS_SCRIPT_COMPAT>
        $<$<BOOL:${SCRIPT_LIB_SD3}>:ENABLE_SD3>
        $<$<BOOL:${SCRIPT_LIB_LUAU}>:ENABLE_LUAU>
        $<$<BOOL:${PLAYERBOTS}>:ENABLE_PLAYERBOTS>
)

target_link_libraries(game
    PUBLIC
        # The terrain engine: geometry and baked tiles, and no knowledge of the
        # world, the database or any DBC.
        terrain
        # The combat rules, on the same terms: numbers in, numbers out, and no
        # knowledge of anything above them.
        combat_pure
        # Where a mover may go and how it gets there. Like terrain, it knows nothing
        # of the world, the database or any DBC -- what a MOVER is allowed to do
        # reaches it as a Nav::MoveProfile value that src/game fills in.
        nav
        proto
        shared
        ZLIB::ZLIB
    PRIVATE
        mangos_openssl_strict
)

# Generate precompiled header
if(PCH)
    add_cxx_pch(game pchdef.h pchdef.cpp)
endif()

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/AuctionHouseBot/ahbot.conf.dist
    DESTINATION ${CONF_INSTALL_DIR}
)


if(PLAYERBOTS)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf.dist
    DESTINATION ${CONF_INSTALL_DIR})
endif()
