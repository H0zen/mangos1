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

#include "MaiEngine.h"

#include "mai/MaiLowering.h"
#include "mai/MaiRunner.h"
#include "mai/MaiTargeting.h"
#include "mai/MaiValidate.h"

#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "WaypointManager.h"
#include "dbscripts/DbScriptStore.h"

#include <algorithm>
#include <cstring>

namespace scripting
{
    namespace
    {
        /**
         * The creature or game object a step's buddy fields name.
         *
         * The half of targeting that needs a world, kept apart from the half
         * that does not -- see MaiTargeting.h. This mirrors the DB scripts'
         * own search exactly, including which grid it visits: a pet is found
         * among world objects and an ordinary creature among grid objects, and
         * the two are different visits rather than two names for one.
         *
         * @return false when the row asked for a buddy and none was found,
         *         which stops the step. A row that asked for none returns true
         *         with @a buddy left null, which is the common case.
         */
        bool FindBuddy(Map* map, mai::Step const& step, WorldObject* source,
                       WorldObject* target, WorldObject*& buddy)
        {
            buddy = nullptr;

            if (!step.buddy.entry)
            {
                return true;
            }

            uint8 const flags = step.buddy.flags;

            if (flags & mai::BuddyByGuid)
            {
                if (CreatureInfo const* info =
                        ObjectMgr::GetCreatureTemplate(step.buddy.entry))
                {
                    Creature* found = map->GetCreature(
                        info->GetObjectGuid(step.buddy.guidOrRadius));

                    // A dead buddy named by guid stops the step rather than
                    // being acted through, which is the original's rule and
                    // not an obvious one: by ENTRY the search simply looks
                    // among the living instead.
                    if (found && !found->IsAlive())
                    {
                        return false;
                    }
                    buddy = found;
                }
                else
                {
                    buddy = map->GetGameObject(ObjectGuid(
                        HIGHGUID_GAMEOBJECT, step.buddy.entry,
                        step.buddy.guidOrRadius));
                }

                return buddy != nullptr;
            }

            if (!source && !target)
            {
                return false;
            }

            // Prefer a non-player as the searcher: a player standing anywhere
            // near the action would otherwise decide what "nearby" means.
            WorldObject* searcher = source ? source : target;
            if (searcher->GetTypeId() == TYPEID_PLAYER && target &&
                target->GetTypeId() != TYPEID_PLAYER)
            {
                searcher = target;
            }

            float const radius = float(step.buddy.guidOrRadius);

            if (ObjectMgr::GetCreatureTemplate(step.buddy.entry))
            {
                Creature* found = nullptr;

                if (flags & mai::BuddyIsDespawned)
                {
                    MaNGOS::AllCreaturesOfEntryInRangeCheck check(
                        searcher, step.buddy.entry, radius);
                    MaNGOS::CreatureLastSearcher<
                        MaNGOS::AllCreaturesOfEntryInRangeCheck>
                            search(found, check);
                    Cell::VisitGridObjects(searcher, search, radius);
                }
                else
                {
                    MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck
                        check(*searcher, step.buddy.entry, true, false, radius,
                              true);
                    MaNGOS::CreatureLastSearcher<
                        MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck>
                            search(found, check);

                    if (flags & mai::BuddyIsPet)
                    {
                        Cell::VisitWorldObjects(searcher, search, radius);
                    }
                    else
                    {
                        Cell::VisitGridObjects(searcher, search, radius);
                    }
                }

                // The original's last resort, kept: a script that names its
                // own entry and finds nobody else meant itself.
                if (!found && searcher->GetEntry() == step.buddy.entry)
                {
                    buddy = searcher;
                    return true;
                }

                buddy = found;
            }
            else
            {
                GameObject* found = nullptr;
                MaNGOS::NearestGameObjectEntryInObjectRangeCheck check(
                    *searcher, step.buddy.entry, radius);
                MaNGOS::GameObjectLastSearcher<
                    MaNGOS::NearestGameObjectEntryInObjectRangeCheck>
                        search(found, check);
                Cell::VisitGridObjects(searcher, search, radius);
                buddy = found;
            }

            return buddy != nullptr;
        }

        /**
         * The subject of a case, as the type that case is about.
         *
         * A Ref is a guid and nothing more -- the seam erases the type on
         * purpose -- so recovering it with a cast would be asking the compiler
         * to take the payload's word for what a slot holds. It compiles for
         * any guid at all, and a Ref naming a player where a creature was
         * meant would give a Creature* pointing at a Player. The map already
         * knows how to answer by type, and answering null for the wrong one is
         * what makes the recovery safe rather than merely correct so far.
         */
        WorldObject* ObjectOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetWorldObject(ObjectGuid(ref.guid));
        }

        Creature* CreatureOn(Context const& ctx, Ref ref)
        {
            // GetAnyTypeCreature, not GetCreature: a pet is a creature to
            // these tables exactly as it is to EventAI.
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetAnyTypeCreature(ObjectGuid(ref.guid));
        }

        GameObject* GameObjectOn(Context const& ctx, Ref ref)
        {
            return (ref.IsEmpty() || !ctx.map)
                       ? nullptr
                       : ctx.map->GetGameObject(ObjectGuid(ref.guid));
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

        WorldObject* Resolve(Map* map, ObjectGuid guid)
        {
            return guid.IsEmpty() ? nullptr : map->GetWorldObject(guid);
        }

        /**
         * Carry out one step.
         *
         * MIGRATION SCAFFOLDING, and the commit that added it says so at
         * length: MAI owns the model, the clock, the targeting and the
         * validation, and hands the effect itself to the body that already
         * exists and is already right. Twelve hundred lines of effects retyped
         * blind, with no differential test yet to catch a transposition, would
         * be the largest unforced risk in this whole exercise.
         *
         * @return true when the sequence should stop here.
         */
        bool Perform(Map* map, mai::Frame const& frame,
                     mai::Step const& step)
        {
            ScriptInfo const* row = static_cast<ScriptInfo const*>(step.origin);
            if (!row)
            {
                return false;
            }

            WorldObject* source = Resolve(map, frame.source);
            WorldObject* target = Resolve(map, frame.target);

            WorldObject* found = nullptr;
            if (!FindBuddy(map, step, source, target, found))
            {
                // The row asked for someone who is not there. The original
                // logs and skips the step; the sequence carries on.
                return false;
            }

            mai::Cast<WorldObject*> cast;
            cast.source = source;
            cast.target = target;
            cast.buddy = found;

            WorldObject* finalSource = nullptr;
            WorldObject* finalTarget = nullptr;
            mai::Redirect(step.buddy.flags, cast, finalSource, finalTarget);

            ScriptAction action(DBScriptType(frame.sequence->origin), map,
                                finalSource ? finalSource->GetObjectGuid()
                                            : ObjectGuid(),
                                finalTarget ? finalTarget->GetObjectGuid()
                                            : ObjectGuid(),
                                frame.owner, row);
            return action.HandleScriptStep();
        }
    }

    void MaiEngine::LoadData(LoadPhase phase)
    {
        // Last: every table a step's parameters are checked against has to be
        // in place before any of them can be checked at all. That ordering is
        // the entire reason LoadPhase exists.
        if (phase != LoadPhase::Final)
        {
            return;
        }

        m_sequences.clear();

        std::size_t sequences = 0;
        std::size_t steps = 0;
        std::size_t refusedSteps = 0;
        std::size_t refusedRows = 0;

        for (int type = DBS_START; type < DBS_END; ++type)
        {
            ScriptChainMap const* chains =
                sDbScripts.GetScriptChainMap(DBScriptType(type));
            if (!chains)
            {
                continue;
            }

            for (ScriptChainMap::const_iterator itr = chains->begin();
                 itr != chains->end(); ++itr)
            {
                char name[64];
                std::snprintf(name, sizeof(name), "db_scripts[%d] id %u",
                              type, itr->first);

                mai::Sequence sequence;
                std::string error;
                if (!mai::Lower(itr->second, itr->first, name, sequence, error))
                {
                    sLog.outErrorDb("MAI: %s: %s", name, error.c_str());
                    ++refusedRows;
                    continue;
                }

                sequence.origin = uint32(type);
                refusedSteps += mai::Validate(sequence);

                steps += sequence.steps.size();
                ++sequences;
                m_sequences.emplace(Key{ uint32(type), itr->first },
                                    std::move(sequence));
            }
        }

        sLog.outString("MAI: %u sequence(s), %u step(s) from the DB scripts; "
                       "%u chain(s) refused, %u step(s) named something this "
                       "world does not have.",
                       uint32(sequences), uint32(steps), uint32(refusedRows),
                       uint32(refusedSteps));
    }

    bool MaiEngine::ReloadData(char const* table)
    {
        // The DB-script tables are MAI's now, so the reload commands are too.
        // The store still reads them; this rebuilds the sequences from what it
        // read.
        static char const* const owned[] =
        {
            "dbscripts_on_quest_start", "dbscripts_on_quest_end",
            "dbscripts_on_spell", "dbscripts_on_go_use",
            "dbscripts_on_go_template_use", "dbscripts_on_event",
            "dbscripts_on_gossip", "dbscripts_on_creature_death",
            "dbscripts_on_creature_movement", "db_script_string",
            "db_scripts",
        };

        bool mine = false;
        for (char const* name : owned)
        {
            if (std::strcmp(table, name) == 0)
            {
                mine = true;
                break;
            }
        }

        if (!mine)
        {
            return false;
        }

        // Safe only because of WHERE a reload runs: on the world thread, with
        // the parallel map update already joined. A frame holds a pointer into
        // m_sequences, so rebuilding it while a map thread walked one would be
        // a use after free.
        m_frames.clear();
        LoadData(LoadPhase::Final);
        return true;
    }

    bool MaiEngine::Start(Map* map, uint32 type, uint32 id, WorldObject* source,
                          WorldObject* target, uint32 unique)
    {
        auto found = m_sequences.find(Key{ type, id });
        if (found == m_sequences.end() || found->second.steps.empty())
        {
            return false;
        }

        ObjectGuid const sourceGuid = source ? source->GetObjectGuid()
                                             : ObjectGuid();
        ObjectGuid const targetGuid = target ? target->GetObjectGuid()
                                             : ObjectGuid();

        // Refuse a second copy while the first is still running, on whichever
        // of the two actors the engine's own policy names. Without this a
        // player clicking a gossip option twice gets the sequence twice, which
        // for a script that summons something means two of it.
        if (unique != Map::SCRIPT_EXEC_PARAM_NONE)
        {
            auto live = m_frames.find(map);
            if (live != m_frames.end())
            {
                for (mai::Frame const& frame : live->second)
                {
                    if (frame.Finished() || frame.sequence != &found->second)
                    {
                        continue;
                    }

                    bool const bySource =
                        (unique & Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE) &&
                        frame.source == sourceGuid;
                    bool const byTarget =
                        (unique & Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_TARGET) &&
                        frame.target == targetGuid;

                    if (bySource || byTarget)
                    {
                        return false;
                    }
                }
            }
        }

        mai::Frame frame;
        frame.sequence = &found->second;
        frame.source = sourceGuid;
        frame.target = targetGuid;

        // An item source is the one thing not findable from its guid, so the
        // player holding it rides along -- the same reason the seam's guid box
        // carries an owner.
        if (source && source->GetTypeId() == TYPEID_PLAYER)
        {
            frame.owner = source->GetObjectGuid();
        }
        else if (target && target->GetTypeId() == TYPEID_PLAYER)
        {
            frame.owner = target->GetObjectGuid();
        }

        m_frames[map].push_back(frame);
        return true;
    }

    void MaiEngine::Tick(Context const& ctx, uint32 diff)
    {
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return;
        }

        auto found = m_frames.find(ctx.map);
        if (found == m_frames.end() || found->second.empty())
        {
            return;
        }

        std::vector<mai::Frame>& frames = found->second;

        // Indexed rather than iterated: a step can start another sequence on
        // this same map, which appends here. An iterator would be invalidated
        // by that; an index simply does not visit the new frame until the next
        // tick, which is also the right answer -- a sequence starting now has
        // had no time pass in it yet.
        std::size_t const wasSize = frames.size();
        for (std::size_t i = 0; i < wasSize && i < frames.size(); ++i)
        {
            mai::Frame& frame = frames[i];
            mai::Runner run(frame, diff);

            while (mai::Step const* step = run.Next())
            {
                if (Perform(ctx.map, frame, *step))
                {
                    run.Stop();
                }
            }
        }

        frames.erase(std::remove_if(frames.begin(), frames.end(),
                         [](mai::Frame const& frame)
                         { return frame.Finished(); }),
                     frames.end());
    }

    void MaiEngine::RetireState(Context const& ctx)
    {
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return;
        }

        // Nothing survives the map. A sequence still running when the last
        // player left is a sequence about a world that is no longer there.
        m_frames.erase(ctx.map);
    }

    Verdict MaiEngine::Dispatch(Context const& ctx, EventId id, Arg* args,
                                std::size_t count)
    {
        // The sequences belong to a map. An event raised in the global scope
        // has no map to run one on, which is not an error -- it simply is not
        // addressed here.
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

                Creature const* victim = CreatureOn(ctx, args[0].AsEntity());
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

                GameObject const* go = GameObjectOn(ctx, args[1].AsEntity());
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

                GameObject const* go = GameObjectOn(ctx, args[1].AsEntity());
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

                Creature const* creature = CreatureOn(ctx, args[0].AsEntity());

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
        // quests, creatures and spells have no script at all.
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

        bool const started = Start(ctx.map, uint32(type), scriptId, sourceObj,
                                   targetObj,
                                   UniquenessFor(type, sourceObj, targetObj));

        // Claiming is the exception, and the reason is unchanged from the
        // engine this replaces: nothing has RUN when a sequence is started, so
        // there is normally no answer to give. Exactly one caller asks a
        // question that can be answered -- EffectTriggerSpell, which logs when
        // nothing at all took the trigger -- and for that one, "was anything
        // queued" is a fair reading of "did an engine deal with this".
        if (id != EventId::SpellEffectHit)
        {
            return Verdict::Continue;
        }

        return started ? Verdict::Handled : Verdict::Continue;
    }
}
