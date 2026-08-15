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

# events.manifest is the single source of truth for the scripting seam, and
# ScriptEvents.gen.h and events.d.luau are committed copies of what it says.
# gen_events.py's own docstring promised that "CI regenerates and diffs, so a
# manifest edit that was not regenerated fails there rather than drifting
# quietly" -- and nothing anywhere ran it. This is that promise.
#
# THIS ADDS NO DEPENDENCY ON PYTHON, and that is a requirement rather than a
# nicety. It is an add_test, so it never runs during a build at all -- only
# under ctest, only when WITH_TESTS is on -- and when there is no usable Python
# it reports that and PASSES. Someone building this server for the first time,
# who has no idea which packages a generator wants, must not be handed a red
# test for a tool they were never asked to install. CI has Python and therefore
# has the check; that is where the check is for.
#
# "Usable" is doing real work in that sentence. On Windows, find_program
# routinely turns up the Microsoft Store's python.exe stub, which is not an
# interpreter: it prints an advert for the Store and exits non-zero. Taken for
# Python it would fail this test on exactly the naive machine the paragraph
# above is about, so the interpreter is asked to prove itself first.

set(GENERATORS
    "${SOURCE_ROOT}/src/game/Scripting/tools/gen_events.py"
    "${SOURCE_ROOT}/src/game/Scripting/mai/tools/gen_actions.py")

foreach(GENERATOR IN LISTS GENERATORS)
    if(NOT EXISTS "${GENERATOR}")
        message(FATAL_ERROR "Generator missing: ${GENERATOR}")
    endif()
endforeach()

find_program(PYTHON_BIN NAMES python3 python)

if(NOT PYTHON_BIN)
    message(STATUS "No Python found; skipping the event manifest check.")
    return()
endif()

execute_process(
    COMMAND "${PYTHON_BIN}" -c "print(1)"
    RESULT_VARIABLE PYTHON_USABLE
    OUTPUT_QUIET ERROR_QUIET)

if(NOT PYTHON_USABLE EQUAL 0)
    message(STATUS "'${PYTHON_BIN}' does not run; skipping the event manifest "
                   "check.")
    return()
endif()

foreach(GENERATOR IN LISTS GENERATORS)
    execute_process(
        COMMAND "${PYTHON_BIN}" "${GENERATOR}" --check
        RESULT_VARIABLE GENERATOR_STATUS
        OUTPUT_VARIABLE GENERATOR_OUTPUT
        ERROR_VARIABLE GENERATOR_ERRORS)

    if(NOT GENERATOR_STATUS EQUAL 0)
        message(FATAL_ERROR
            "A manifest and its generated files disagree.\n"
            "Run: python ${GENERATOR}\n"
            "${GENERATOR_OUTPUT}${GENERATOR_ERRORS}")
    endif()

    message(STATUS "${GENERATOR_OUTPUT}")
endforeach()
