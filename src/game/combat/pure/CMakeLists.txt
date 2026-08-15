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

# =============================================================================
# combat_pure -- the combat rules, and nothing else.
#
# Sits beside geometry at the bottom of the graph: it depends on the standard
# library and on nothing in this repository. No Unit, no World, no DBC, no
# database, no packet.
#
# That is not tidiness. It is the only reason the combat rules can be tested at
# all: mangos_tests links this library WITHOUT linking `game`, so a hit table
# can be built from two plain structs and rolled against an injected number. The
# old path could only be exercised by standing a server up, which is why the
# audit found thirty-three defects and zero tests.
#
# CheckCombatBoundary.cmake fails the suite the moment an include crosses back.
# =============================================================================

add_library(combat_pure STATIC
    CombatTypes.h
    CombatConstants.h
    Profile.h
    Rng.h
    Strike.h
    Matchup.h
    Matchup.cpp
    HitTable.h
    HitTable.cpp
    StrikeResolver.h
    StrikeResolver.cpp
    SpellFacts.h
    ProcPoints.h
    ProcPoints.cpp
    ProcTrigger.h
    ProcTrigger.cpp
)

# Consumers spell it #include "combat/pure/HitTable.h", so the root is src/game.
target_include_directories(combat_pure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

set_target_properties(combat_pure PROPERTIES
    FOLDER "game"
    POSITION_INDEPENDENT_CODE ON
)
