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

#include "DbScriptEngine.h"

#include "GameObject.h"
#include "Map.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "WaypointManager.h"

namespace scripting
{
    namespace
    {
        /**
         * Resolve a Ref to the object the schedule will act on.
         *
         * ScriptsStart takes pointers and converts them straight back to guids
         * for the ScriptAction it queues, so this round trip looks wasteful. It
         * is not: it is the check that the object is still on this map before
         * anything is scheduled against it.
         */
        WorldObject* ObjectOn(Context const& ctx, Ref ref)
        {
            if (ref.IsEmpty() || !ctx.map)
            {
                return nullptr;
            }

            return ctx.map->GetWorldObject(ObjectGuid(ref.guid));
        }

        /// Which of the two actors the dedup key is built from.
        ///
        /// This is the engine's own policy about its own queues -- whether a
        /// second copy of the same script may run while the first is pending --
        /// and it has no business being decided by the world.
        Map::ScriptExecutionParam UniquenessFor(DBScriptType type,
                                                WorldObject const* source,
                                                WorldObject const* target)
        {
            switch (type)
            {
                case DBS_ON_QUEST_START:
                case DBS_ON_QUEST_END:
                    return Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE;

                case DBS_ON_GOSSIP:
                    // Keyed on whichever end is the creature or object: two
                    // players talking to one NPC must not share a key.
                    return (source
                            && source->isType(TYPEMASK_CREATURE_OR_GAMEOBJECT))
                               ? Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE
                               : Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_TARGET;

                case DBS_ON_EVENT:
                    if (source
                        && source->isType(TYPEMASK_CREATURE_OR_GAMEOBJECT))
                    {
                        return Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE;
                    }
                    if (target
                        && target->isType(TYPEMASK_CREATURE_OR_GAMEOBJECT))
                    {
                        return Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_TARGET;
                    }
                    return Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE_TARGET;

                default:
                    return Map::SCRIPT_EXEC_PARAM_NONE;
            }
        }

        /// The script id bound to a chosen gossip option.
        ///
        /// The world hands over which menu and which line; finding the script
        /// behind them is a read of this engine's own table.
        uint32 GossipScriptId(uint32 menuId, uint32 gossipListId)
        {
            GossipMenuItemsMapBounds bounds =
                sObjectMgr.GetGossipMenuItemsMapBounds(menuId);

            for (GossipMenuItemsMap::const_iterator itr = bounds.first;
                 itr != bounds.second; ++itr)
            {
                if (itr->second.id == gossipListId)
                {
                    return itr->second.action_script_id;
                }
            }

            return 0;
        }

        /// The script id on the gossip menu row the conditions selected.
        uint32 GossipMenuScriptId(uint32 menuId, uint32 textId)
        {
            GossipMenusMapBounds bounds =
                sObjectMgr.GetGossipMenusMapBounds(menuId);

            for (GossipMenusMap::const_iterator itr = bounds.first;
                 itr != bounds.second; ++itr)
            {
                if (itr->second.text_id == textId)
                {
                    return itr->second.script_id;
                }
            }

            return 0;
        }

        /// The script id on a waypoint node.
        uint32 WaypointScriptId(Creature const* creature, int32 pathId,
                                uint32 pathOrigin, uint32 nodeIndex)
        {
            if (!creature)
            {
                return 0;
            }

            WaypointPath const* path = sWaypointMgr.GetPathFromOrigin(
                creature->GetEntry(), creature->GetGUIDLow(), pathId,
                static_cast<WaypointPathOrigin>(pathOrigin));
            if (!path)
            {
                return 0;
            }

            WaypointPath::const_iterator node = path->find(nodeIndex);
            return node != path->end() ? node->second.script_id : 0;
        }
    }

    Verdict DbScriptEngine::Dispatch(Context const& ctx, EventId id, Arg* args,
                                     std::size_t count)
    {
        // The schedule belongs to a map. An event raised in the global scope
        // has no schedule to append to, which is not an error -- it simply is
        // not addressed to this engine.
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return Verdict::Continue;
        }

        DBScriptType type = DBS_END;
        uint32 scriptId = 0;
        Ref source{ 0 };
        Ref target{ 0 };

        switch (id)
        {
            case EventId::PlayerQuestStart:
            case EventId::PlayerQuestEnd:
            {
                MANGOS_ASSERT(count == PlayerQuestStart::Arity);

                Quest const* quest = nullptr;
                Handle const handle = args[2].AsNamed();
                if (handle.domain == Domain::Quest)
                {
                    quest = sObjectMgr.GetQuestTemplate(
                        static_cast<uint32>(handle.id));
                }

                if (!quest)
                {
                    return Verdict::Continue;
                }

                bool const start = (id == EventId::PlayerQuestStart);
                type = start ? DBS_ON_QUEST_START : DBS_ON_QUEST_END;
                scriptId = start ? quest->GetQuestStartScript()
                                 : quest->GetQuestCompleteScript();
                source = args[1].AsEntity();     // the quest giver
                target = args[0].AsEntity();     // the player
                break;
            }

            case EventId::CreatureDied:
            {
                MANGOS_ASSERT(count == CreatureDied::Arity);

                Creature const* victim = static_cast<Creature const*>(
                    ObjectOn(ctx, args[0].AsEntity()));
                if (!victim)
                {
                    return Verdict::Continue;
                }

                type = DBS_ON_CREATURE_DEATH;
                scriptId = victim->GetEntry();
                source = args[0].AsEntity();
                target = args[1].AsEntity();
                break;
            }

            case EventId::GameobjectUse:
            {
                MANGOS_ASSERT(count == GameobjectUse::Arity);

                GameObject const* go = static_cast<GameObject const*>(
                    ObjectOn(ctx, args[1].AsEntity()));
                if (!go)
                {
                    return Verdict::Continue;
                }

                // Keyed by TEMPLATE ENTRY. The guid-keyed table belongs to
                // on_activate, which is a later and narrower moment -- only
                // doors, buttons and goobers reach it, and only after they
                // have operated. Firing both here would run guid scripts for
                // object types that never used to see them.
                type = DBS_ON_GOT_USE;
                scriptId = go->GetEntry();
                source = args[0].AsEntity();
                target = args[1].AsEntity();
                break;
            }

            case EventId::GameobjectActivate:
            {
                MANGOS_ASSERT(count == GameobjectActivate::Arity);

                GameObject const* go = static_cast<GameObject const*>(
                    ObjectOn(ctx, args[1].AsEntity()));
                if (!go)
                {
                    return Verdict::Continue;
                }

                // Keyed by GUID: this table is per placed object, not per
                // template.
                type = DBS_ON_GO_USE;
                scriptId = go->GetGUIDLow();
                source = args[0].AsEntity();
                target = args[1].AsEntity();
                break;
            }

            case EventId::GossipActionChosen:
            {
                MANGOS_ASSERT(count == GossipActionChosen::Arity);

                scriptId = GossipScriptId(
                    static_cast<uint32>(args[2].AsNumber()),
                    static_cast<uint32>(args[3].AsNumber()));
                type = DBS_ON_GOSSIP;
                source = args[1].AsEntity();     // the gossip source
                target = args[0].AsEntity();     // the player
                break;
            }

            case EventId::GossipMenuShown:
            {
                MANGOS_ASSERT(count == GossipMenuShown::Arity);

                scriptId = GossipMenuScriptId(
                    static_cast<uint32>(args[2].AsNumber()),
                    static_cast<uint32>(args[3].AsNumber()));
                type = DBS_ON_GOSSIP;
                source = args[0].AsEntity();     // the player
                target = args[1].AsEntity();     // the gossip source
                break;
            }

            case EventId::SpellEffectHit:
            {
                MANGOS_ASSERT(count == SpellEffectHit::Arity);

                type = DBS_ON_SPELL;
                scriptId = static_cast<uint32>(args[2].AsNumber());
                source = args[0].AsEntity();
                target = args[1].AsEntity();
                break;
            }

            case EventId::ServerEventRaised:
            {
                MANGOS_ASSERT(count == ServerEventRaised::Arity);

                type = DBS_ON_EVENT;
                scriptId = static_cast<uint32>(args[2].AsNumber());
                source = args[0].AsEntity();
                target = args[1].AsEntity();
                break;
            }

            case EventId::CreatureReachWp:
            {
                MANGOS_ASSERT(count == CreatureReachWp::Arity);

                Creature const* creature = static_cast<Creature const*>(
                    ObjectOn(ctx, args[0].AsEntity()));

                type = DBS_ON_CREATURE_MOVEMENT;
                scriptId = WaypointScriptId(
                    creature,
                    static_cast<int32>(args[1].AsSigned()),
                    static_cast<uint32>(args[2].AsNumber()),
                    static_cast<uint32>(args[3].AsNumber()));
                source = args[0].AsEntity();
                target = args[0].AsEntity();
                break;
            }

            default:
                return Verdict::Continue;
        }

        // No row for this subject is the ordinary case, not a failure: most
        // quests, creatures and spells have no DB script at all.
        if (!scriptId)
        {
            return Verdict::Continue;
        }

        WorldObject* sourceObj = ObjectOn(ctx, source);
        WorldObject* targetObj = ObjectOn(ctx, target);
        if (!sourceObj && !targetObj)
        {
            return Verdict::Continue;
        }

        bool const started = ctx.map->ScriptsStart(
            type, scriptId, sourceObj, targetObj,
            UniquenessFor(type, sourceObj, targetObj));

        // Handled, not a payload field. The one caller that asks is a spell
        // effect that logs an error when nothing took the trigger at all, and
        // "did an engine deal with this" is exactly what Handled means.
        return started ? Verdict::Handled : Verdict::Continue;
    }
}
