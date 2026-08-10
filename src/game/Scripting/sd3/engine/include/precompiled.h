/**
 * ScriptDev3 is an extension for mangos providing enhanced features for
 * area triggers, creatures, game objects, instances, items, and spells beyond
 * the default database scripting in mangos.
 *
 * Copyright (C) 2014-2026 MaNGOS <https://www.getmangos.eu>
 * Copyright (C) 2006-2013 ScriptDev2 <http://www.scriptdev2.com/>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#ifndef SC_PRECOMPILED_H
#define SC_PRECOMPILED_H

// The single prelude for every script: all 480 of them include this file by
// name, and nothing else here is textual.
//
// There used to be a pch.h beside it that precompiled this file and carried a
// SECOND copy of the list under this same guard, so whichever the compiler saw
// first silently suppressed the other and the two drifted -- a PCH build and a
// non-PCH build then compiled the scripts against different headers. pch.h is
// gone: these sources are part of `game` now and share its precompiled header,
// so there is one list and it is this one.
//
#include "system/ScriptDevMgr.h"
#include "ScriptMgr.h"
#include "Object.h"
#include "ObjectGuid.h"
#include "Unit.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "sc_creature.h"
#include "sc_gossip.h"
#include "sc_grid_searchers.h"
#include "sc_instance.h"
#include "SpellAuras.h"
#include "World.h"

// Common.h is being retired across the cores; these are the pieces the scripts
// used to receive through it. Named here once rather than in 480 script files.
//
// This list was also mirrored outside the tree, in src/shared/Compat/sd3, and
// force-included into every source from the build system -- the only way to
// adapt sources that could not be edited, back when they were a submodule
// pinned to an upstream commit. They can be edited now, so the shim is gone
// and this is the only copy.
#include "Common/TimeConstants.h"
#include "Utilities/MathDefines.h"
#include "Utilities/Util.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#endif
