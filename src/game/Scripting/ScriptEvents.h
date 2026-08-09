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

#ifndef MANGOS_SCRIPT_EVENTS_H
#define MANGOS_SCRIPT_EVENTS_H

/**
 * Every event the world can raise.
 *
 * There is nothing to edit here. The events are declared once, in
 * `events.manifest`, and `tools/gen_events.py` turns that one file into the
 * EventId enum, the payload structs, the editor type stubs for script authors
 * and the reference table. Three artefacts that used to be maintained by hand
 * in three places, and that drifted apart exactly as often as you would
 * expect: the census that produced the manifest found 213 distinct ids across
 * four systems, two of which had been maintaining near-identical hook surfaces
 * side by side for years, each with its own gaps.
 *
 * An event declares four things about itself, and the first three are what
 * make a mistake a compile error rather than a convention:
 *
 *   Id           which slot in the dispatch table it is
 *   Arity        how many payload cells it uses
 *   Cancellable  a hook may refuse the action        -> only Ask() takes it
 *   Claimable    a hook may state it produced the
 *                behaviour, so the core skips its
 *                own default                         -> only Offer() takes it
 *
 * `Pack` writes the payload, `Unpack` reads back whatever a hook changed. An
 * engine that writes into a slot MUST keep the slot's Kind; the accessors
 * assert on a mismatch rather than reinterpreting bits.
 *
 * @see events.manifest for the declarations and the policy vocabulary
 */
#include "ScriptEvents.gen.h"

#endif //MANGOS_SCRIPT_EVENTS_H
