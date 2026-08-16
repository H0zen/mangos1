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
# A pure half depends on the standard library and on the other pure halves.
# Nothing else.
#
# THE LINKER WILL NOT HOLD THIS SEAM. A single #include "Unit.h" for one
# convenient enum compiles perfectly inside `game`, and the damage shows up
# somewhere else entirely: the next person to build the tests discovers that
# the half which was supposed to be testable on a table now needs the whole
# server. Nothing fails at the moment the mistake is made, which is why it has
# to be read rather than linked.
#
# ONE ENTRY PER PURE HALF, in the list below. That is the whole of what makes
# this a foundation instead of a habit -- see COMPONENTS.md F3, which asks
# every extraction to produce a pure/stateful pair, and F4, which says the pair
# erodes in three months without this.
#
# Allowed: angle-bracket standard headers, and quoted headers that exist in
# this directory or in any other directory on the list. Two pure halves may
# lean on each other; neither may lean on the world.
# =============================================================================

set(PURE_DIRS
    "src/game/combat/pure"
    "src/game/Aura/pure"
)

# Resolve to absolute, and drop the ones that are not there yet: a half that
# has not been extracted is not a failure, it is a step not taken.
set(PRESENT_DIRS "")
foreach(RELATIVE IN LISTS PURE_DIRS)
  if(EXISTS "${SOURCE_ROOT}/${RELATIVE}")
    list(APPEND PRESENT_DIRS "${SOURCE_ROOT}/${RELATIVE}")
  else()
    message(STATUS "No ${RELATIVE} yet; nothing to hold there")
  endif()
endforeach()

if(NOT PRESENT_DIRS)
  message(STATUS "No pure halves yet; boundary check has nothing to hold")
  return()
endif()

set(VIOLATIONS "")
set(TOTAL 0)

foreach(DIR IN LISTS PRESENT_DIRS)
  file(RELATIVE_PATH DIR_NAME "${SOURCE_ROOT}" "${DIR}")
  file(GLOB SOURCES "${DIR}/*.h" "${DIR}/*.cpp")

  list(LENGTH SOURCES COUNT)
  math(EXPR TOTAL "${TOTAL} + ${COUNT}")

  foreach(SOURCE IN LISTS SOURCES)
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

      # Any pure directory, not only this one, so a half may be written in
      # terms of another half without either of them reaching for the world.
      set(FOUND FALSE)
      foreach(CANDIDATE IN LISTS PRESENT_DIRS)
        if(EXISTS "${CANDIDATE}/${HEADER}")
          set(FOUND TRUE)
          break()
        endif()
      endforeach()

      if(NOT FOUND)
        list(APPEND VIOLATIONS "${DIR_NAME}/${SOURCE_NAME}: ${HEADER}")
      endif()
    endforeach()
  endforeach()
endforeach()

if(VIOLATIONS)
  string(REPLACE ";" "\n  " REPORT "${VIOLATIONS}")
  message(FATAL_ERROR
    "A pure half reached outside itself:\n  ${REPORT}\n"
    "These link into mangos_tests without the game library. Pass the value in "
    "as an argument, or through the view the stateful half builds, instead of "
    "including the world.")
endif()

list(LENGTH PRESENT_DIRS DIR_COUNT)
message(STATUS
  "Pure boundary intact: ${TOTAL} file(s) in ${DIR_COUNT} half/halves depend "
  "on nothing but std and each other")
