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
#include "dbscripts/DbScriptStore.h"

#include <algorithm>

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
        // Not owned here. DbScriptEngine reloads the tables; this rebuilds
        // from whatever they now hold, and says so rather than claiming the
        // name -- two engines answering one reload command is the collision
        // ReloadData's first-match-wins chain cannot report.
        (void)table;
        return false;
    }

    void MaiEngine::Start(Map* map, uint32 type, uint32 id, WorldObject* source,
                          WorldObject* target)
    {
        auto found = m_sequences.find(Key{ type, id });
        if (found == m_sequences.end() || found->second.steps.empty())
        {
            return;
        }

        mai::Frame frame;
        frame.sequence = &found->second;
        frame.source = source ? source->GetObjectGuid() : ObjectGuid();
        frame.target = target ? target->GetObjectGuid() : ObjectGuid();

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
        // Deliberately silent for now. MAI is loaded, validated and ticking,
        // and starting sequences from events is the next step -- but doing it
        // while DbScriptEngine also does would run every DB script twice, and
        // a differential test that changes the world twice is not a test.
        (void)ctx; (void)id; (void)args; (void)count;
        return Verdict::Continue;
    }
}
