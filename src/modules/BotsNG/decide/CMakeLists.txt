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
# The BotsNG decision core.
#
# LINKS NOTHING, and the target is written this way so that stays true. No
# `shared`, no `game`, no MySQL, no OpenSSL: a bot's decision is a pure
# function from a snapshot to a set of intents, and the moment somebody
# includes Player.h in this directory the library stops building on its own and
# says so at once -- rather than three months later, when the reason the bots
# cannot be tested is again that they need a server running.
#
# It configures even when BUILD_MANGOSD is off, which is what lets the unit
# tests link it in a tree with no server in it. The half of BotsNG that DOES
# know about the world -- sensing and executing -- is built with the module
# instead, and depends on this; never the other way round.
# =============================================================================

set(BOTSNG_DECIDE_SRCS
    Percept.h
    Intent.h
    IntentSink.h
    Arbiter.h
    Arbiter.cpp
    Policy.h
    Policy.cpp
    layers/Survival.h
    layers/Survival.cpp
)

source_group("decide" FILES ${BOTSNG_DECIDE_SRCS})

add_library(botsng_decide STATIC ${BOTSNG_DECIDE_SRCS})

target_include_directories(botsng_decide
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_features(botsng_decide PUBLIC cxx_std_17)

set_target_properties(botsng_decide PROPERTIES
    FOLDER "modules"
    POSITION_INDEPENDENT_CODE ON)
