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

# Nothing here builds a target of its own any more.
#
# SD3 used to: a `mangosscript` static library that linked `game` while `game`
# linked it back, a cycle that only held together because static libraries
# forgive one. It is a scripting engine now and lives where the other engines
# live, in src/game/Scripting/sd3/, compiled straight into `game` -- so the
# cycle is gone rather than tolerated.
#
# Bots is the one module left and it never had a target either: `game` globs
# its sources directly. The directory stays for it.
#
# This file exists so `add_subdirectory(modules)` in src/CMakeLists.txt keeps
# working and so the next module has an obvious place to land.
