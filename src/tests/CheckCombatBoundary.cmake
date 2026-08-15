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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

# =============================================================================
# The pure combat core depends on the standard library and on its own headers.
# Nothing else.
#
# The linker will not hold this seam on its own: a single #include "Unit.h" for
# one convenient enum compiles fine inside `game`, and the next person to try
# building the tests discovers the core now needs the whole server. So the seam
# is held here, by reading the includes.
#
# Allowed: angle-bracket standard headers, and quoted headers that exist in this
# same directory.
# =============================================================================

set(PURE_DIR "${SOURCE_ROOT}/src/game/combat/pure")

if(NOT EXISTS "${PURE_DIR}")
  message(STATUS "No pure combat core yet; boundary check has nothing to hold")
  return()
endif()

file(GLOB PURE_SOURCES "${PURE_DIR}/*.h" "${PURE_DIR}/*.cpp")

set(VIOLATIONS "")

foreach(SOURCE IN LISTS PURE_SOURCES)
  get_filename_component(SOURCE_NAME "${SOURCE}" NAME)
  file(STRINGS "${SOURCE}" INCLUDE_LINES REGEX "^[ \t]*#[ \t]*include")

  foreach(LINE IN LISTS INCLUDE_LINES)
    # <foo> is the standard library. We do not police which parts.
    if(LINE MATCHES "#[ \t]*include[ \t]*<")
      continue()
    endif()

    string(REGEX MATCH "\"([^\"]+)\"" QUOTED "${LINE}")
    if(NOT QUOTED)
      continue()
    endif()
    set(HEADER "${CMAKE_MATCH_1}")

    # A sibling in this directory is the core including itself.
    if(EXISTS "${PURE_DIR}/${HEADER}")
      continue()
    endif()

    list(APPEND VIOLATIONS "${SOURCE_NAME}: ${HEADER}")
  endforeach()
endforeach()

if(VIOLATIONS)
  string(REPLACE ";" "\n  " REPORT "${VIOLATIONS}")
  message(FATAL_ERROR
    "The pure combat core reached outside itself:\n  ${REPORT}\n"
    "combat_pure links into mangos_tests without the game library. Pass the "
    "value in through Profile or an argument instead of including the world.")
endif()

list(LENGTH PURE_SOURCES PURE_COUNT)
message(STATUS
  "Combat core boundary intact: ${PURE_COUNT} file(s) depend on nothing but std")
