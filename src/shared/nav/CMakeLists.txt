# SPDX-License-Identifier: GPL-3.0-or-later
#
# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
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
# nav -- where a mover may go, and how it gets there.
#
# The baker and the runtime share every one of these files, which is the point:
# a cell index computed while writing a tile and a cell index computed while
# reading one are the same arithmetic, not two agreeing implementations.
#
# It links terrain and nothing else. It must never gain a dependency on the
# database, the world, or a Unit -- what a MOVER is allowed to do arrives as a
# Nav::MoveProfile value, filled in by the game, so that every routing decision
# can be asserted on in a test that stands up no server at all.
# =============================================================================

add_library(nav STATIC
    NavArea.hpp
    NavBuilder.cpp
    NavBuilder.hpp
    NavGrid.hpp
    NavMesh.cpp
    NavMesh.hpp
    NavPolygons.cpp
    NavPolygons.hpp
    NavStore.cpp
    NavStore.hpp
    NavTile.cpp
    NavTile.hpp
    NavTileIO.cpp
    NavTileIO.hpp
    Route.hpp
    Router.cpp
    Router.hpp
    SearchBudget.hpp
)

# Consumers spell it #include "nav/NavTile.hpp", so the root is src/shared.
target_include_directories(nav
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nav
    PUBLIC
        terrain
        geometry
)
