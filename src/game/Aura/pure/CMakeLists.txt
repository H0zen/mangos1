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
# aura_pure -- the aura rules that are arithmetic, and nothing else.
#
# The second pure half, built the same way combat_pure is and for the same
# reason: mangos_tests links it WITHOUT linking `game`, so a rule can be
# exercised from plain values instead of from a running server. The aura
# system has zero tests today, and every one of its rules is reachable only
# through two god classes -- this is where that stops, one extraction at a
# time.
#
# CheckPureBoundary.cmake fails the suite the moment an include crosses back.
# =============================================================================

add_library(aura_pure STATIC
    SlotAllocator.h
    SlotAllocator.cpp
)

# Consumers spell it #include "Aura/pure/SlotAllocator.h", so the root is
# src/game.
target_include_directories(aura_pure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

set_target_properties(aura_pure PROPERTIES
    FOLDER "game"
    POSITION_INDEPENDENT_CODE ON
)
