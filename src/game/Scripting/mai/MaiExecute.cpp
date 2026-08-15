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

// One step, carried out.
//
// The order below is the order the DB scripts have always used, and none of it
// is free to move:
//
//   1. resolve the run's guids, because anything may have died since it started
//   2. find the buddy, or -- for a rule -- ask the selector who it means
//   3. rearrange source, target and buddy by the four flags, IN ORDER
//   4. the native body if the verb has one
//   5. otherwise the borrowed body, through a row written back out
//
// Step 2 is where the two systems meet. A `dbscripts_on_*` row names its third
// object by ENTRY and a radius; an EventAI row names it by a question about
// the threat list. Both produce one object, and both hand it to the same
// rearrangement -- which is why the selector was put on the Step beside the
// buddy rather than being given a resolution rule of its own. `cast` wants the
// creature to act ON the selected unit and `set_unit_field` wants the selected
// unit to BE the actor, and that distinction already had a flag.

#include "MaiExecute.h"

#include "MaiCompile.h"
#include "MaiLowering.h"
#include "MaiPerform.h"
#include "MaiTargeting.h"

#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "dbscripts/DbScripts.h"

#include <iterator>
#include <list>

namespace mai
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
        bool FindBuddy(Map* map, Step const& step, WorldObject* source,
                       WorldObject* target, WorldObject*& buddy)
        {
            buddy = nullptr;

            if (!step.buddy.entry)
            {
                return true;
            }

            uint8 const flags = step.buddy.flags;

            if (flags & BuddyByGuid)
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

                if (flags & BuddyIsDespawned)
                {
                    MaNGOS::AllCreaturesOfEntryInRangeCheck check(
                        searcher, step.buddy.entry, radius);
                    MaNGOS::CreatureLastSearcher<
                        MaNGOS::AllCreaturesOfEntryInRangeCheck>
                            search(found, check);
                    Cell::VisitGridObjects(searcher, search, radius);
                }
                else if (flags & BuddyRandom)
                {
                    // Every living one, then one of them. The nearest is the
                    // wrong answer for an encounter that wants any of its
                    // adds: it is always the same add.
                    std::list<Creature*> all;
                    MaNGOS::AllCreaturesOfEntryInRangeCheck check(
                        searcher, step.buddy.entry, radius);
                    MaNGOS::CreatureListSearcher<
                        MaNGOS::AllCreaturesOfEntryInRangeCheck>
                            search(all, check);
                    Cell::VisitGridObjects(searcher, search, radius);

                    for (std::list<Creature*>::iterator it = all.begin();
                         it != all.end(); )
                    {
                        it = (*it)->IsAlive() ? ++it : all.erase(it);
                    }

                    if (!all.empty())
                    {
                        std::list<Creature*>::iterator pick = all.begin();
                        std::advance(pick, urand(0, uint32(all.size() - 1)));
                        found = *pick;
                    }
                }
                else
                {
                    MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck
                        check(*searcher, step.buddy.entry, true, false, radius,
                              true);
                    MaNGOS::CreatureLastSearcher<
                        MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck>
                            search(found, check);

                    if (flags & BuddyIsPet)
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
            return (!map || guid.IsEmpty()) ? nullptr
                                            : map->GetWorldObject(guid);
        }

        /**
         * The spell a selection should avoid choosing an immune target for.
         *
         * Only a cast asks, and only a cast can answer: the selectors take a
         * spell id so that "a random player on the threat list" does not pick
         * one the spell cannot land on. Everything else passes zero, which
         * means the selection considers all of them.
         */
        uint32 SpellUnder(Step const& step)
        {
            return (step.action == ActionId::CastSpell && step.Has(0))
                       ? step.operands[0].u
                       : 0;
        }
    }

    bool Execute(Run const& run, Step const& step)
    {
        // A branch is not a thing that happens in the world, and this is the
        // only place that could mistake one for a thing that does. MaiRunner
        // resolves every control verb itself and hands out none of them, so
        // arriving here means somebody ran a step list without one -- and the
        // fall-through below would look up a borrowed body for `end`, find
        // none, and log a puzzle. Refusing costs a comparison and keeps the
        // rule stated where it can be read: control flow belongs to the frame,
        // never to a verb.
        if (IsControl(step.action))
        {
            return false;
        }

        // Before anything is resolved: a step that is not going to happen
        // should not search the grid to discover whom it would not have
        // happened to.
        if (step.chance == 0 || (step.chance < 100 && step.chance <= urand(0, 99)))
        {
            return false;
        }

        WorldObject* source = Resolve(run.map, run.source);
        WorldObject* target = Resolve(run.map, run.target);

        // Whose RULE this is, remembered before anything moves it. A source
        // selector replaces `source` outright, and `credit_owner` means "the
        // creature that decided", not "whoever ended up acting" -- so reading
        // it off `source` afterwards would credit the wrong unit exactly when
        // the step went out of its way to say otherwise.
        WorldObject* const decider = source;

        WorldObject* found = nullptr;
        if (!FindBuddy(run.map, step, source, target, found))
        {
            // The row asked for someone who is not there. The original logs
            // and skips the step; the sequence carries on.
            return false;
        }

        if (run.fromRule)
        {
            // A rule names its third object by a question rather than by an
            // entry, and the answer is ordinary enough to be absent: a boss
            // fighting one player has no second-highest threat. Skipping the
            // step is what EventAI did, and it is not an error.
            bool missing = false;
            Unit* picked = nullptr;

            if (step.select == SelectRemembered)
            {
                // Not one of EventAI's ten, so it is answered here rather than
                // in MaiSelect: it is the only selector that asks the ACTOR
                // rather than the world, and MaiSelect deliberately knows
                // nothing about a creature's memory.
                picked = (run.actor && run.map)
                             ? run.map->GetUnit(run.actor->remembered)
                             : nullptr;
                missing = picked == nullptr;
            }
            else
            {
                picked = Select(source ? source->ToCreature() : nullptr,
                                step.select, run.from, missing,
                                SpellUnder(step), step.selectFlags);
            }
            // The second choice, when there was one. ScriptDev writes this by
            // hand at every selection that has a sensible fallback, and
            // without it the ability does not happen at all on the pulls where
            // the first choice is empty.
            if (!picked && step.selectElse != SelectNone)
            {
                missing = false;
                picked = step.selectElse == SelectRemembered
                             ? ((run.actor && run.map)
                                    ? run.map->GetUnit(run.actor->remembered)
                                    : nullptr)
                             : Select(source ? source->ToCreature() : nullptr,
                                      step.selectElse, run.from, missing,
                                      SpellUnder(step), step.selectFlags);
            }

            if (!picked)
            {
                if (missing)
                {
                    DEBUG_FILTER_LOG(LOG_FILTER_EVENT_AI_DEV,
                                     "MAI: selector %u found nobody for %s",
                                     uint32(step.select),
                                     source ? source->GetGuidStr().c_str()
                                            : "<none>");
                }
                return false;
            }

            found = picked;

            // And whom it acts AS, when the step says. Answered with the same
            // selectors and against the same creature -- "a random player" is
            // the same question whether it names the actor or the acted-upon.
            //
            // Not finding anybody here is not a reason to skip: a step whose
            // source selector is empty falls back to the creature whose rule
            // it is, which is what the step meant before the column existed.
            if (step.selectSource != SelectNone)
            {
                bool missingSource = false;
                Unit* actor =
                    step.selectSource == SelectRemembered
                        ? ((run.actor && run.map)
                               ? run.map->GetUnit(run.actor->remembered)
                               : nullptr)
                        : Select(source ? source->ToCreature() : nullptr,
                                 Selector(step.selectSource), run.from,
                                 missingSource,
                                 SpellUnder(step), step.selectFlags);

                if (actor)
                {
                    source = actor;
                }
            }
        }

        Cast<WorldObject*> cast;
        cast.source = source;
        cast.target = target;
        cast.buddy = found;

        WorldObject* finalSource = nullptr;
        WorldObject* finalTarget = nullptr;
        Redirect(step.buddy.flags, cast, finalSource, finalTarget);

        // The native verbs first. A step MAI implements itself never reaches
        // ScriptAction, which is what lets the borrowed bodies be retired one
        // at a time: each verb that grows a native body simply stops falling
        // through.
        Doing doing;
        doing.map = run.map;
        doing.source = finalSource;
        doing.target = finalTarget;
        doing.owner = run.owner;
        doing.actor = run.actor;
        doing.driver = run.driver;
        doing.refused = run.refused;
        doing.item = run.item;
        doing.cancel = run.cancel;

        // Before Redirect moved anything: who this is being done FOR.
        doing.ruleOwner = decider;

        bool handled = false;
        bool const stop = PerformNative(doing, step, handled);
        if (handled)
        {
            return stop;
        }

        // The borrowed body needs a row. A step lowered from `dbscripts_on_*`
        // still has the one it came from; a step lowered from a RULE never had
        // one and gets it written out here. Both are scaffolding and both go
        // when the last borrowed body does.
        ScriptInfo const* row = static_cast<ScriptInfo const*>(step.origin);

        ScriptInfo raised;
        if (!row)
        {
            std::string error;
            if (!Raise(step, raised, error))
            {
                sLog.outErrorDb("MAI: %s", error.c_str());
                return false;
            }
            row = &raised;
        }

        ScriptAction action(DBScriptType(run.origin), run.map,
                            finalSource ? finalSource->GetObjectGuid()
                                        : ObjectGuid(),
                            finalTarget ? finalTarget->GetObjectGuid()
                                        : ObjectGuid(),
                            run.owner, row);
        return action.HandleScriptStep();
    }
}
