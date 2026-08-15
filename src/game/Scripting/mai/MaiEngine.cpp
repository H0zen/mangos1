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

#include "mai/MaiCompile.h"
#include "mai/MaiCreatureAI.h"
#include "mai/MaiExecute.h"
#include "mai/MaiLowering.h"
#include "mai/MaiParse.h"
#include "mai/MaiRunner.h"
#include "mai/MaiValidate.h"

#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
// Not for a pointer -- the seam forward-declares it -- but for GetData, which
// a guard about an instance's own state has to call.
#include "InstanceData.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Item.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SharedDefines.h"
#include "WaypointManager.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scripting
{
    namespace
    {
        /// What `creature_template.AIName` says to reach this engine.
        ///
        /// Its OWN name, not EventAI's, and that is the migration plan rather
        /// than an oversight: the two engines bid on different names, so a
        /// single creature can be moved across by changing one column and
        /// moved back by changing it again. A flag day for 5,822 creatures,
        /// with no way to compare the two side by side, is not a thing to do
        /// to a live world.
        char const AI_NAME[] = "MAI";

        /**
         * A borrowed pointer, refused unless it is still inside its call.
         *
         * An Aura has no identity to hand out and no lifetime an engine can
         * reason about, so the seam lends it rather than naming it: an engine
         * that stored one and read it next tick would find a stale epoch here
         * instead of freed memory.
         */
        template <class T>
        T* Borrowed(Borrow const& borrow, Domain domain)
        {
            if (borrow.domain != domain || !detail::IsBorrowLive(borrow))
            {
                return nullptr;
            }

            return static_cast<T*>(borrow.target);
        }

        /**
         * The lowest effect index of @a spell that applies a dummy aura.
         *
         * What "EFFECT_INDEX_0" meant in the eight scripts that opened with
         * it: run once when the aura goes on, not once per effect. Written as
         * a question about the spell so that a spell whose dummy sits at index
         * 1 is handled rather than silently skipped.
         */
        SpellEffectIndex FirstDummyEffect(SpellEntry const* spell)
        {
            if (spell)
            {
                for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
                {
                    if (spell->EffectAura[effect] == SPELL_AURA_DUMMY)
                    {
                        return SpellEffectIndex(effect);
                    }
                }
            }
            return EFFECT_INDEX_0;
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

        /**
         * Carry out one step of a sequence the WORLD started.
         *
         * A thin call now that a creature's AI runs steps too: what a step
         * does belongs to mai::Execute, and this only says which of the Run's
         * fields a world-started sequence fills in. Which is most of the
         * point -- the two callers differ in what they know, not in what a
         * step means.
         *
         * @return true when the sequence should stop here.
         */
        bool Perform(Map* map, mai::Frame const& frame, mai::Step const& step)
        {
            mai::Run run;
            run.map = map;
            run.source = frame.source;
            run.target = frame.target;
            run.owner = frame.owner;
            run.item = frame.item;
            run.origin = frame.sequence ? frame.sequence->origin : 0;

            // No creature behind it, so no Actor and no selector: a queued
            // command list was told its target when it was queued.
            run.actor = nullptr;
            run.fromRule = false;

            return mai::Execute(run, step);
        }

        /**
         * What a guard can be asked about a sequence the WORLD started.
         *
         * Less than a creature can answer, and the difference is the point of
         * the interface: there is no Actor here, so there is nothing that
         * remembers anything, so a bare name -- `enraged=0` -- is unanswerable
         * rather than zero. The loader already refuses to parse one into a
         * `mai_step` row for that reason; this is the same answer at the other
         * end, in case a sequence ever acquires one another way.
         *
         * `aura:` is the source's and `target_aura:` is the target's, which is
         * the only reading available when the two ends are all there is.
         */
        struct WorldSight : public mai::Sight
        {
            Map*       map = nullptr;
            ObjectGuid source;
            ObjectGuid target;

            bool Ask(mai::Guard const& guard, uint32& held) const override
            {
                if (!map)
                {
                    return false;
                }

                if (guard.of == mai::GuardInstance)
                {
                    InstanceData* data = map->GetInstanceData();
                    if (!data)
                    {
                        // Zero is a real encounter state -- NOT_STARTED -- so
                        // "there is no instance" must not read as it, or an
                        // `instance:6=0` guard holds in the open world.
                        return false;
                    }

                    held = data->GetData(guard.subject);
                    return true;
                }

                if (guard.of == mai::GuardAura ||
                    guard.of == mai::GuardTargetAura)
                {
                    ObjectGuid const& whose = guard.of == mai::GuardAura
                                                  ? source : target;
                    Unit* who = whose.IsEmpty() ? nullptr
                                                : map->GetUnit(whose);
                    if (!who)
                    {
                        return false;
                    }

                    // Stacks, not presence: absent is zero, so `aura:9438=0`
                    // means "not under it" and `>=3` means what it says.
                    SpellAuraHolder* holder =
                        who->GetSpellAuraHolder(guard.subject);
                    held = holder ? holder->GetStackAmount() : 0;
                    return true;
                }

                return false;
            }
        };
    }

    MaiEngine* MaiEngine::s_instance = nullptr;

    MaiEngine::MaiEngine()
    {
        s_instance = this;
    }

    MaiEngine::~MaiEngine()
    {
        if (s_instance == this)
        {
            s_instance = nullptr;
        }
    }

    bool MaiEngine::StartFrom(Map* map, uint32 kind, uint32 id,
                              WorldObject* source, WorldObject* target,
                              ObjectGuid owner, ObjectGuid item, bool* cancel)
    {
        if (!s_instance || !map)
        {
            return false;
        }

        // A branch of an INLINE sequence runs inline too, and that is not an
        // optimisation: the caller is about to answer a question -- may this
        // item be used, was this trigger handled -- and a branch that queued
        // itself would answer after the answer was given.
        if (cancel)
        {
            if (s_instance->RunNow(map, kind, id, source, target, owner, item))
            {
                *cancel = true;
            }
            return true;
        }

        // A branch may run only once per step, so uniqueness is nobody's
        // policy here: whoever started the parent already decided that.
        return s_instance->Start(map, kind, id, source, target,
                                 Map::SCRIPT_EXEC_PARAM_NONE, owner, item);
    }

    mai::Sequence const* MaiEngine::Find(uint32 kind, uint32 id)
    {
        if (!s_instance)
        {
            return nullptr;
        }

        // Read-only at run time: the sequences are built at start-up and
        // rebuilt only by a reload, which runs on the world thread with the
        // parallel map update joined and clears every frame pointing into them
        // first. So no lock, and none is missing.
        auto found = s_instance->m_sequences.find(Key{ kind, id });
        return found != s_instance->m_sequences.end() ? &found->second
                                                      : nullptr;
    }

    std::vector<mai::Frame>& MaiEngine::FramesOf(Map const* map)
    {
        std::lock_guard<std::mutex> guard(m_framesLock);
        return m_frames[map];
    }

    /**
     * Whether this world database has MAI's tables at all.
     *
     * Asked once, in one query, and the reason is what happens otherwise: a
     * world that has not run the migration yet answers five SELECTs with
     * "Table 'x.mai_step' doesn't exist", which the database layer logs as ten
     * lines of `SQL:` / `query ERROR:` before each loader shrugs and reports
     * the table as EMPTY. Empty and absent are the same silence to the loader
     * and completely different facts to whoever is reading the log in the
     * middle of a migration -- one is a world with no scripts, the other is a
     * world whose scripts have not been converted yet.
     *
     * @return false when ANY of them is missing. A half-migrated schema is not
     *         a state to load through: the rules would come up without the
     *         sequences they start, or the other way round.
     */
    bool MaiEngine::HasSchema()
    {
        static char const* const tables[] =
        {
            "mai_script", "mai_rule", "mai_step", "mai_rule_step", "mai_text",
        };
        std::size_t const wanted = sizeof(tables) / sizeof(*tables);

        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT COUNT(*) FROM `information_schema`.`tables` "
            "WHERE `table_schema` = DATABASE() AND `table_name` IN "
            "('mai_script', 'mai_rule', 'mai_step', 'mai_rule_step', "
            "'mai_text')"));

        std::size_t const found =
            result ? std::size_t(result->Fetch()[0].GetUInt32()) : 0;

        if (found == wanted)
        {
            return true;
        }

        sLog.outErrorDb("MAI: this world database has %u of MAI's %u tables. "
                        "Nothing is scripted -- no creature has an AI and no "
                        "sequence can start -- until the migration that "
                        "creates and fills them has been applied.",
                        uint32(found), uint32(wanted));
        return false;
    }

    void MaiEngine::LoadData(LoadPhase phase)
    {
        // Last: every table a step's parameters are checked against has to be
        // in place before any of them can be checked at all.
        if (phase != LoadPhase::Final)
        {
            return;
        }

        if (!HasSchema())
        {
            return;
        }

        LoadTexts();
        LoadSequences();

        // ONCE, and never from a reload. Every live MaiCreatureAI holds
        // pointers into m_rules -- that is the whole point of keeping the
        // rules per entry rather than per creature -- so rebuilding it under
        // them is a use after free on every creature currently in the world.
        // The sequences above have no such problem because a reload clears the
        // frames that point into them first.
        LoadRules();
    }

    void MaiEngine::LoadTexts()
    {
        // `mai_text`, which is the three text tables merged with not one id
        // changed -- their ranges were disjoint and stayed disjoint, so the
        // merge is a union and every existing reference still points at what
        // it pointed at.
        //
        // THE WHOLE TABLE, and it has to be. This read the creature-AI range
        // alone, because `db_script_string` was still being loaded by the
        // DB-script store and `script_texts` by SD3, and loading a range twice
        // was what that avoided. Both of those are gone -- the store with this
        // change, SD3 whenever it is configured out -- so their ranges arrive
        // here or nowhere. On a live world that was 3,010 of 4,026 rows
        // refused one line at a time: every ported ScriptDev script's speech
        // and every DB-script `talk`.
        sLog.outString("Loading MAI texts...");
        sObjectMgr.LoadMangosStrings(WorldDatabase, "mai_text",
                                     ANY_TEXT_STRING_ID, ANY_TEXT_STRING_ID,
                                     true);
    }

    void MaiEngine::LoadSequences()
    {
        // `mai_script` and `mai_step`, read as they are written.
        //
        // This lowered `db_scripts` in memory at every start-up until now. The
        // RULES moved off that path when EventAI left and the sequences
        // quietly did not, so every row written into `mai_step` since then sat
        // in a table nothing read: ported gameobjects and spell effects that
        // loaded, validated, and never ran.
        //
        // BUILT BESIDE THE LIVE TABLE, not into it. Clearing first and reading
        // afterwards means a reload against a database that has gone away ends
        // with a world that has no scripts at all -- the query fails, the
        // function returns, and what is left is the empty table it made on its
        // way in. What replaces the sequences is a set of sequences.
        std::unordered_map<Key, mai::Sequence, KeyHash> loaded;

        std::size_t sequences = 0;
        std::size_t steps = 0;
        std::size_t refusedSteps = 0;
        std::size_t refusedRows = 0;

        // The ENUM's own order, which is the order the column declares it in.
        // Spelled out rather than derived: `kind` is what a person types and
        // DBScriptType is what the borrowed bodies expect, and the two exist
        // for different reasons while only happening to agree.
        // The last three are MAI's own and have no `dbscripts_on_*` table
        // behind them, so their `origin` -- what a BORROWED body should think
        // it is -- is the nearest thing that does. An aura going on or coming
        // off is a spell effect as far as those bodies can tell, and a branch
        // is whatever started it.
        static struct { char const* name; uint32 type; } const kinds[] =
        {
            { "quest_start",       DBS_ON_QUEST_START },
            { "quest_end",         DBS_ON_QUEST_END },
            { "spell",             DBS_ON_SPELL },
            { "go_use",            DBS_ON_GO_USE },
            { "go_template_use",   DBS_ON_GOT_USE },
            { "creature_death",    DBS_ON_CREATURE_DEATH },
            { "creature_movement", DBS_ON_CREATURE_MOVEMENT },
            { "gossip",            DBS_ON_GOSSIP },
            { "event",             DBS_ON_EVENT },
            { "internal",          DBS_ON_CREATURE_SPELL },
            { "aura_apply",        mai::KindAuraApply },
            { "aura_remove",       mai::KindAuraRemove },
            { "branch",            mai::KindBranch },
            { "item_use",          mai::KindItemUse },
            { "areatrigger",       mai::KindAreaTrigger },
        };
        std::size_t const kindCount = sizeof(kinds) / sizeof(*kinds);

        // Every step in one query, keyed as the sequences are so the join is a
        // lookup rather than a query per script.
        //
        // The guards travel WITH the steps rather than being read afterwards,
        // because a step names a window into its sequence's guard table and
        // that window is only meaningful beside the table it indexes. Two
        // collections that had to be zipped up later would be two chances to
        // get the offsets wrong.
        struct Draft
        {
            std::vector<mai::Step>  steps;
            std::vector<mai::Guard> guards;
        };

        std::map<std::pair<uint32, uint32>, Draft> byScript;
        {
            // `seq` is selected only to be able to NAME the row in an error.
            // (kind, script, seq) is the primary key, and a refusal that says
            // "aura_apply script 10848" leaves whoever reads it to find which
            // of that script's steps was meant.
            std::unique_ptr<QueryResult> rows(WorldDatabase.Query(
                "SELECT `kind`+0, `script`, `at_ms`, `action`, `params`, "
                "`buddy_entry`, `buddy_range`, `buddy_flags`, `chance`, `seq`, "
                "`guard` "
                "FROM `mai_step` ORDER BY `kind`, `script`, `seq`"));

            while (rows && rows->NextRow())
            {
                Field* field = rows->Fetch();

                // MySQL numbers an ENUM from 1; the table above from 0.
                uint32 const which = field[0].GetUInt32();
                if (which == 0 || which > kindCount)
                {
                    ++refusedSteps;
                    continue;
                }

                uint32 const type = kinds[which - 1].type;
                uint32 const script = field[1].GetUInt32();

                mai::Step step;
                std::string error;
                if (!mai::Parse(field[3].GetString(), field[4].GetString(),
                                step, error))
                {
                    sLog.outErrorDb("MAI: %s script %u seq %u: %s",
                                    kinds[which - 1].name, script,
                                    field[9].GetUInt32(), error.c_str());
                    ++refusedSteps;
                    continue;
                }

                step.atMs = field[2].GetUInt32();
                step.buddy.entry = field[5].GetUInt32();
                step.buddy.guidOrRadius = field[6].GetUInt32();
                step.buddy.flags = field[7].GetUInt8();

                // Out of 100, and 100 is "always". A step's own roll, not the
                // sequence's: `random_script` is how a script picks ONE of
                // several, and this is how it says "and sometimes a third".
                //
                // ZERO IS NEVER, and is left alone. Rewriting it to 100 was
                // the two tables disagreeing about one field: MaiScript says
                // never, Execute rolls it as never, mai_rule_step loads it as
                // never -- and this one line turned "never" into "always" for
                // the sequences alone. The column defaults to 100, so a zero
                // is somebody typing one.
                step.chance = field[8].GetUInt8();
                if (step.chance > 100)
                {
                    step.chance = 100;
                }

                Draft& draft = byScript[std::make_pair(type, script)];

                // WHETHER, beside the step's WHAT and WHEN. The same column a
                // rule has carried since there were rules, and the same
                // parser: `if` is a verb whose guard decides a jump, and an
                // ordinary step's guard decides whether that one line runs.
                //
                // No owner, because a sequence the world starts has no
                // creature -- so `instance:`, `aura:` and `target_aura:` are
                // askable here and a bare name is refused with the same words
                // `set_state` uses.
                step.guardFirst = uint16(draft.guards.size());
                if (!mai::ParseGuards(field[10].GetString(), draft.guards,
                                      nullptr, error))
                {
                    sLog.outErrorDb("MAI: %s script %u seq %u: %s",
                                    kinds[which - 1].name, script,
                                    field[9].GetUInt32(), error.c_str());
                    draft.guards.resize(step.guardFirst);
                    ++refusedSteps;
                    continue;
                }
                std::size_t const guards =
                    draft.guards.size() - step.guardFirst;
                if (guards > 0xFF)
                {
                    sLog.outErrorDb("MAI: %s script %u seq %u: more guards "
                                    "than one step may carry",
                                    kinds[which - 1].name, script,
                                    field[9].GetUInt32());
                    draft.guards.resize(step.guardFirst);
                    ++refusedSteps;
                    continue;
                }
                step.guardCount = uint8(guards);

                draft.steps.push_back(step);
            }
        }

        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT `kind`+0, `id`, `name` FROM `mai_script` "
            "ORDER BY `kind`, `id`"));

        if (!result)
        {
            sLog.outString("MAI: `mai_script` gave no rows; nothing the world "
                           "does starts a sequence. Whatever was already "
                           "loaded is left as it was.");
            return;
        }

        do
        {
            Field* field = result->Fetch();

            uint32 const which = field[0].GetUInt32();
            if (which == 0 || which > kindCount)
            {
                ++refusedRows;
                continue;
            }

            uint32 const type = kinds[which - 1].type;
            uint32 const id = field[1].GetUInt32();

            mai::Sequence sequence;
            sequence.id = id;
            sequence.origin = type;
            sequence.kind = kinds[which - 1].name;
            sequence.name = field[2].GetString();

            auto found = byScript.find(std::make_pair(type, id));
            if (found != byScript.end())
            {
                sequence.steps = std::move(found->second.steps);
                sequence.guards = std::move(found->second.guards);

                // A TIMELINE IS SORTED AND A PROGRAM IS NOT, and the script
                // itself says which it is by whether anything in it branches.
                //
                // The runner stops at the first step not yet due, so an
                // unsorted timeline drops everything after the first
                // out-of-order row: `seq` orders the query, and this orders
                // the clock. But sorting a program by time would move a step
                // out of the block it belongs to and leave the jumps pointing
                // at whatever landed at that index -- so a script with control
                // in it keeps the order it was written in, and MaiCompile
                // checks the times instead of rearranging them.
                if (mai::Branches(sequence.steps))
                {
                    std::string error;
                    if (!mai::Compile(sequence, error))
                    {
                        // Refused whole. Half a program is not a smaller
                        // program: an `if` whose `end` is missing would run
                        // its body unconditionally, which is the one outcome
                        // worse than the script not running at all.
                        sLog.outErrorDb("MAI: %s %u: %s", sequence.kind,
                                        sequence.id, error.c_str());
                        refusedSteps += sequence.steps.size();
                        sequence.steps.clear();
                        sequence.guards.clear();
                        sequence.program = false;
                    }
                }
                else
                {
                    std::stable_sort(sequence.steps.begin(),
                                     sequence.steps.end(),
                                     [](mai::Step const& a, mai::Step const& b)
                                     { return a.atMs < b.atMs; });
                }
            }

            refusedSteps += mai::Validate(sequence);

            steps += sequence.steps.size();
            ++sequences;
            loaded.emplace(Key{ type, id }, std::move(sequence));
        }
        while (result->NextRow());

        m_sequences = std::move(loaded);

        sLog.outString("MAI: %u sequence(s), %u step(s); %u refused, %u step(s) "
                       "refused or naming something this world does not have.",
                       uint32(sequences), uint32(steps), uint32(refusedRows),
                       uint32(refusedSteps));
    }

    void MaiEngine::LoadRules()
    {
        // `mai_rule` and `mai_rule_step`, read as they are written: a trigger
        // by name, its parameters as `name=value`, and the steps that follow.
        //
        // This used to convert `creature_ai_scripts` in memory at every
        // start-up, which is what let the rule engine be built against twenty
        // thousand rows before there was a table to hold them. The table holds
        // them now.
        m_rules.clear();

        std::size_t creatures = 0;
        std::size_t rules = 0;
        std::size_t refused = 0;
        std::size_t refusedSteps = 0;

        // Ordered by creature so that a rule set is built in one pass, and by
        // id so that two rules of one creature keep the order they were
        // written in -- which is the order they fire in when both are due.
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT `creature`, `id`, `rule`, `params`, `phase_mask`, "
            "`chance`, `flags`, `guard`, `retry` FROM `mai_rule` "
            "ORDER BY `creature`, `id`"));

        if (!result)
        {
            sLog.outString("MAI: `mai_rule` is empty; no creature is driven "
                           "by rules.");
            return;
        }

        // Every rule's steps, in one query rather than one per rule. Twenty
        // thousand round trips at start-up is a minute of a server's life
        // spent on something a single scan answers.
        //
        // Read BEFORE the rules, and that ordering is load-bearing now: a
        // `set_state name=enraged` step interns "enraged" into the creature's
        // own name table, and a guard reading `enraged=0` has to find the same
        // slot. Both go through the RuleSet, so whichever is met first creates
        // it and the other finds it.
        struct Draft
        {
            std::vector<mai::Step>  steps;
            std::vector<mai::Guard> guards;
        };

        std::map<std::pair<uint32, uint32>, Draft> steps;
        {
            std::unique_ptr<QueryResult> stepRows(WorldDatabase.Query(
                "SELECT `creature`, `rule`, `action`, `params`, `select`, "
                "`buddy_flags`, `select_flags`, `buddy_entry`, `buddy_range`, "
                "`chance`, `at_ms`, `select_else`, `select_source`, `seq`, "
                "`guard` "
                "FROM `mai_rule_step` "
                "ORDER BY `creature`, `rule`, `seq`"));

            while (stepRows && stepRows->NextRow())
            {
                Field* field = stepRows->Fetch();
                uint32 const creature = field[0].GetUInt32();
                uint32 const rule = field[1].GetUInt32();

                mai::RuleSet& owner = m_rules[creature];
                owner.creature = creature;

                mai::Step step;
                std::string error;
                if (!mai::Parse(field[2].GetString(), field[3].GetString(),
                                step, owner, error))
                {
                    sLog.outErrorDb("MAI: creature %u rule %u seq %u: %s",
                                    creature, rule, field[13].GetUInt32(),
                                    error.c_str());
                    ++refusedSteps;
                    continue;
                }

                // Neither is a parameter of the verb: both modify the step.
                step.select = mai::Selector(field[4].GetUInt8());
                step.buddy.flags = field[5].GetUInt8();
                step.selectFlags = field[6].GetUInt8();
                step.buddy.entry = field[7].GetUInt32();
                step.buddy.guidOrRadius = field[8].GetUInt32();
                step.chance = field[9].GetUInt8();
                step.atMs = field[10].GetUInt32();

                uint8 const orElse = field[11].GetUInt8();
                step.selectElse = mai::Selector(orElse);
                if (orElse != mai::SelectNone && orElse >= mai::SelectEnd)
                {
                    sLog.outErrorDb("MAI: creature %u rule %u seq %u: %u is "
                                    "not a target selector", creature, rule,
                                    field[13].GetUInt32(), uint32(orElse));
                    ++refusedSteps;
                    continue;
                }

                // Whom the step acts AS. The same selectors answering the same
                // question about the other end, so it is checked the same way.
                uint8 const asWhom = field[12].GetUInt8();
                step.selectSource = mai::Selector(asWhom);
                if (asWhom != mai::SelectNone && asWhom >= mai::SelectEnd)
                {
                    sLog.outErrorDb("MAI: creature %u rule %u seq %u: %u is "
                                    "not a source selector", creature, rule,
                                    field[13].GetUInt32(), uint32(asWhom));
                    ++refusedSteps;
                    continue;
                }

                if (step.select >= mai::SelectEnd)
                {
                    sLog.outErrorDb("MAI: creature %u rule %u seq %u: %u is "
                                    "not a target selector", creature, rule,
                                    field[13].GetUInt32(),
                                    uint32(step.select));
                    ++refusedSteps;
                    continue;
                }

                Draft& draft = steps[std::make_pair(creature, rule)];

                // The step's own guard, interned into the SAME creature the
                // rule's guard and its `set_state` steps intern into -- so
                // `enraged` means one slot whether it is set by a step, tested
                // by a rule, or tested by an `if` three rows further down.
                step.guardFirst = uint16(draft.guards.size());
                if (!mai::ParseGuards(field[14].GetString(), draft.guards,
                                      &owner, error))
                {
                    sLog.outErrorDb("MAI: creature %u rule %u seq %u: %s",
                                    creature, rule, field[13].GetUInt32(),
                                    error.c_str());
                    draft.guards.resize(step.guardFirst);
                    ++refusedSteps;
                    continue;
                }

                std::size_t const guards =
                    draft.guards.size() - step.guardFirst;
                if (guards > 0xFF)
                {
                    sLog.outErrorDb("MAI: creature %u rule %u seq %u: more "
                                    "guards than one step may carry", creature,
                                    rule, field[13].GetUInt32());
                    draft.guards.resize(step.guardFirst);
                    ++refusedSteps;
                    continue;
                }
                step.guardCount = uint8(guards);

                draft.steps.push_back(step);
            }
        }

        do
        {
            Field* field = result->Fetch();
            uint32 const creature = field[0].GetUInt32();
            uint32 const id = field[1].GetUInt32();

            mai::Rule rule;
            std::string error;
            if (!mai::Parse(field[2].GetString(), field[3].GetString(), rule,
                            error))
            {
                sLog.outErrorDb("MAI: creature %u rule %u: %s", creature, id,
                                error.c_str());
                ++refused;
                continue;
            }

            rule.id = id;
            rule.inversePhaseMask = field[4].GetUInt32();
            rule.chance = field[5].GetUInt8();
            rule.flags = field[6].GetUInt8();
            rule.retryMs = field[8].GetUInt32();

            mai::RuleSet& set = m_rules[creature];
            set.creature = creature;

            if (!mai::ParseGuards(field[7].GetString(), rule, set, error))
            {
                sLog.outErrorDb("MAI: creature %u rule %u: %s", creature, id,
                                error.c_str());
                ++refused;
                continue;
            }

            auto found = steps.find(std::make_pair(creature, id));
            if (found != steps.end())
            {
                rule.steps.steps = std::move(found->second.steps);
                rule.steps.guards = std::move(found->second.guards);

                // A timeline is sorted and a program is not -- the same
                // decision LoadSequences makes, for the same reason, and made
                // by the same two functions rather than by a second copy of
                // the argument. See there.
                if (mai::Branches(rule.steps.steps))
                {
                    if (rule.flags & mai::RuleRandomStep)
                    {
                        // `random_step` runs ONE of the steps and no more,
                        // which was EventAI's only randomness and is a switch
                        // with the arms hidden. There is no coherent answer
                        // for what it means to pick one row out of a program
                        // -- an `else` on its own, an `end` with nothing open
                        // -- so the combination is refused rather than given
                        // one.
                        sLog.outErrorDb("MAI: creature %u rule %u: a rule that "
                                        "picks one step at random cannot also "
                                        "branch", creature, id);
                        ++refused;
                        continue;
                    }

                    std::string trouble;
                    if (!mai::Compile(rule.steps, trouble))
                    {
                        sLog.outErrorDb("MAI: creature %u rule %u: %s",
                                        creature, id, trouble.c_str());
                        refusedSteps += rule.steps.steps.size();
                        ++refused;
                        continue;
                    }
                }
                else
                {
                    std::stable_sort(rule.steps.steps.begin(),
                                     rule.steps.steps.end(),
                                     [](mai::Step const& a, mai::Step const& b)
                                     { return a.atMs < b.atMs; });
                }
            }

            rule.steps.id = id;

            // Held up against the world exactly as a sequence is: a rule
            // naming a spell this build does not have is refused now rather
            // than logged every time the creature is pulled.
            refusedSteps += mai::Validate(rule.steps);

            set.rules.push_back(std::move(rule));
            ++rules;
        }
        while (result->NextRow());

        creatures = m_rules.size();

        sLog.outString("MAI: %u rule(s) over %u creature(s); %u refused, "
                       "%u step(s) refused or naming something this world "
                       "does not have.",
                       uint32(rules), uint32(creatures), uint32(refused),
                       uint32(refusedSteps));
    }

    int MaiEngine::Bid(Context const& ctx, RoleId role, Ref subject)
    {
        if (role != RoleId::CreatureAI || ctx.scope != Context::Scope::Map ||
            !ctx.map)
        {
            return NoBid;
        }

        Creature const* creature = CreatureOn(ctx, subject);
        if (!creature)
        {
            return NoBid;
        }

        // Not a totem, for the reason EventAI is not: the world picked TotemAI
        // by NPC flag before the AI registry was ever consulted, and the
        // auction runs before those flags are read. The refusal is inherited
        // along with the rules.
        if (creature->IsTotem())
        {
            return NoBid;
        }

        // BidNormal, not BidStrong: the binding is per TEMPLATE ENTRY, and an
        // engine holding a script bound to one particular creature knows more
        // about it than a row naming every copy of the entry.
        return creature->GetAIName() == AI_NAME ? BidNormal : NoBid;
    }

    CreatureAI* MaiEngine::MakeCreatureAI(Context const& ctx,
                                          Creature* creature)
    {
        (void)ctx;

        if (!creature)
        {
            return nullptr;
        }

        // Never declines, and an entry with no rules is not a refusal: an
        // AIName pointing at an empty rule set has always meant a creature
        // that does nothing in particular, which is a thing worth being able
        // to say.
        auto found = m_rules.find(creature->GetEntry());
        return new mai::MaiCreatureAI(
            creature, found != m_rules.end() ? &found->second : nullptr);
    }

    bool MaiEngine::ReloadData(char const* table)
    {
        // MAI's own tables, and only those. This used to answer to the ten
        // `dbscripts_on_*` names as well, with a comment saying the store
        // still read them and this rebuilt the sequences from what it read --
        // which stopped being true when LoadSequences started reading
        // `mai_script` and `mai_step` directly. Answering to a table name it
        // does not read is worse than not answering: the administrator is told
        // the reload happened.
        //
        // `mai_step` is not listed separately on purpose. A sequence is its
        // steps; there is no reload of one without the other, and a name that
        // reloaded half of a sequence would be a trap.
        static char const* const owned[] =
        {
            "mai_script",
            "mai_text",
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

        // The name is MAI's, so the answer is yes even when the reload cannot
        // happen -- returning false here would tell the administrator that no
        // engine owns `mai_script`, which is not what is wrong.
        if (!HasSchema())
        {
            return true;
        }

        // Safe only because of WHERE a reload runs: on the world thread, with
        // the parallel map update already joined. A frame holds a pointer into
        // m_sequences, so rebuilding it while a map thread walked one would be
        // a use after free.
        if (std::strcmp(table, "mai_text") == 0)
        {
            LoadTexts();
            return true;
        }

        {
            std::lock_guard<std::mutex> guard(m_framesLock);
            m_frames.clear();
        }

        // The SEQUENCES only. `mai_rule` is deliberately absent from the list
        // above and cannot be reloaded at all: every live MaiCreatureAI holds
        // pointers into the rule sets, so rebuilding them under a populated
        // world is a use after free on every creature in it. Reloading rules
        // needs the creatures rebuilt too, which is a restart.
        LoadSequences();
        return true;
    }

    bool MaiEngine::Start(Map* map, uint32 type, uint32 id, WorldObject* source,
                          WorldObject* target, uint32 unique, ObjectGuid owner,
                          ObjectGuid item)
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

        std::vector<mai::Frame>& frames = FramesOf(map);

        // Refuse a second copy while the first is still running, on whichever
        // of the two actors the engine's own policy names. Without this a
        // player clicking a gossip option twice gets the sequence twice, which
        // for a script that summons something means two of it.
        if (unique != Map::SCRIPT_EXEC_PARAM_NONE)
        {
            for (mai::Frame const& live : frames)
            {
                if (live.Finished() || live.sequence != &found->second)
                {
                    continue;
                }

                // EVERY dimension the flags name, together. The map's own
                // constant is UNIQUE_BY_SOURCE_TARGET = 0x03, "the same script
                // for the same source AND the same target", and refusing on
                // either half alone means something else entirely: two players
                // talking to one quest giver share a source, so the second one
                // was told nothing at all. Each term is vacuously true when
                // its bit is not asked for, which is what keeps the
                // single-bit flags meaning what they always meant.
                bool const sourceHolds =
                    !(unique & Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE) ||
                    live.source == sourceGuid;
                bool const targetHolds =
                    !(unique & Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_TARGET) ||
                    live.target == targetGuid;

                if (sourceHolds && targetHolds)
                {
                    return false;
                }
            }
        }

        mai::Frame frame;
        frame.sequence = &found->second;
        frame.source = sourceGuid;
        frame.target = targetGuid;
        frame.item = item;

        // An item source is the one thing not findable from its guid, so the
        // player holding it rides along -- the same reason the seam's guid box
        // carries an owner.
        if (!owner.IsEmpty())
        {
            // A branch inherits it. The player holding an item is not findable
            // from either actor once the sequence has moved on from them.
            frame.owner = owner;
        }
        else if (source && source->GetTypeId() == TYPEID_PLAYER)
        {
            frame.owner = source->GetObjectGuid();
        }
        else if (target && target->GetTypeId() == TYPEID_PLAYER)
        {
            frame.owner = target->GetObjectGuid();
        }

        frames.push_back(frame);
        return true;
    }

    bool MaiEngine::RunNow(Map* map, uint32 type, uint32 id,
                           WorldObject* source, WorldObject* target,
                           ObjectGuid owner, ObjectGuid item)
    {
        auto found = m_sequences.find(Key{ type, id });
        if (found == m_sequences.end() || found->second.steps.empty())
        {
            return false;
        }

        mai::Sequence const& sequence = found->second;

        bool cancelled = false;
        std::size_t at = 0;

        mai::Run run;
        run.map = map;
        run.source = source ? source->GetObjectGuid() : ObjectGuid();
        run.target = target ? target->GetObjectGuid() : ObjectGuid();
        run.owner = owner;
        run.item = item;
        run.origin = sequence.origin;
        run.cancel = &cancelled;

        // The steps at time zero, in order, stopping where one says to. The
        // rest -- if the sequence has any -- is an ordinary queued frame that
        // starts from where this left off.
        for (; at < sequence.steps.size(); ++at)
        {
            if (sequence.steps[at].atMs != 0)
            {
                break;
            }

            if (mai::Execute(run, sequence.steps[at]))
            {
                // Stopped. Whether it stopped because it refused or because a
                // guard failed, nothing after it runs.
                return cancelled;
            }
        }

        if (at >= sequence.steps.size())
        {
            return cancelled;
        }

        mai::Frame frame;
        frame.sequence = &sequence;
        frame.next = at;
        frame.source = run.source;
        frame.target = run.target;
        frame.owner = owner;

        // Which item this was about. The inline half had it in the Run and the
        // queued half did not, so "refuse the use, and two seconds later say
        // why" lost the item between its two sentences -- and every verb that
        // asks the owner's bags for it got an empty guid.
        frame.item = item;

        FramesOf(map).push_back(frame);
        return cancelled;
    }

    void MaiEngine::Tick(Context const& ctx, uint32 diff)
    {
        if (ctx.scope != Context::Scope::Map || !ctx.map)
        {
            return;
        }

        // The lock covers the LOOKUP and nothing else. What it is there for is
        // another map's thread inserting its own first frame at this instant,
        // which rehashes the container; the vector it hands back belongs to
        // this map and only this thread ever touches it, and the reference
        // outlives any rehash because the container is node-based.
        std::vector<mai::Frame>* held = nullptr;
        {
            std::lock_guard<std::mutex> guard(m_framesLock);

            auto found = m_frames.find(ctx.map);
            if (found == m_frames.end())
            {
                return;
            }

            held = &found->second;
        }

        std::vector<mai::Frame>& frames = *held;
        if (frames.empty())
        {
            return;
        }

        // Indexed rather than iterated: a step can start another sequence on
        // this same map, which appends here. An iterator would be invalidated
        // by that; an index simply does not visit the new frame until the next
        // tick, which is also the right answer -- a sequence starting now has
        // had no time pass in it yet.
        std::size_t const wasSize = frames.size();
        for (std::size_t i = 0; i < wasSize && i < frames.size(); ++i)
        {
            mai::Frame& frame = frames[i];

            WorldSight sight;
            sight.map = ctx.map;
            sight.source = frame.source;
            sight.target = frame.target;

            mai::Runner run(frame, diff, &sight);

            while (mai::Step const* step = run.Next())
            {
                if (Perform(ctx.map, frame, *step))
                {
                    run.Stop();
                }
            }

            if (run.Exhausted())
            {
                // Not an error and not the end of the sequence: the frame kept
                // its place and will carry on next tick. It is said out loud
                // because the only way to reach it is a loop that is not
                // getting anywhere, and a creature quietly burning a tick's
                // worth of steps for ever is exactly the thing nobody notices.
                sLog.outErrorDb("MAI: %s %u ran %u steps in one tick without "
                                "finishing; a loop in it is not advancing.",
                                frame.sequence ? frame.sequence->kind : "?",
                                frame.sequence ? frame.sequence->id : 0,
                                uint32(mai::MaxStepsPerTick));
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
        std::lock_guard<std::mutex> guard(m_framesLock);
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

                // And the quest giver's own RULES, which is the other half of
                // this moment and had no way in at all. `quest_accepted` and
                // `quest_completed` convert, load, arm, and are treated by the
                // rule engine as "it happened" -- and nothing anywhere ever
                // said that it had, because the world raises no CreatureAI
                // callback for a quest. Both triggers were dead rows.
                //
                // Here rather than on a new callback because this IS the
                // moment: the same event, the same two objects, and the
                // sequence started below is the same fact told the other way.
                if (Creature* giver = CreatureOn(ctx, source))
                {
                    if (mai::MaiCreatureAI* ai =
                            dynamic_cast<mai::MaiCreatureAI*>(giver->AI()))
                    {
                        WorldObject* who = ObjectOn(ctx, target);
                        ai->QuestFor(who ? who->ToPlayer() : nullptr,
                                     quest->GetQuestId(), start);
                    }
                }
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

            case EventId::ServerEventTrigger:
            {
                MANGOS_ASSERT(count == ServerEventTrigger::Arity);

                WorldObject* who = ObjectOn(ctx, args[0].AsEntity());
                Handle const handle = args[1].AsNamed();
                if (!who || !who->ToPlayer() ||
                    handle.domain != Domain::AreaTrigger)
                {
                    return Verdict::Continue;
                }

                // Inline, because the seam asks a question: a trigger script
                // answers `true` when it produced the behaviour, and a
                // sequence that has not run yet has no answer to give.
                //
                // And claiming is a DECISION, not a side effect. A claimed
                // area trigger skips the quest credit the trigger would
                // otherwise give, the tavern rest, the battleground handling
                // and the teleport -- so only a sequence that says `claim`
                // claims, and every one of the six scripts this replaces makes
                // that call differently on different paths.
                return RunNow(ctx.map, mai::KindAreaTrigger,
                              static_cast<uint32>(handle.id), who, who,
                              who->GetObjectGuid(), ObjectGuid())
                           ? Verdict::Handled
                           : Verdict::Continue;
            }

            case EventId::ItemUse:
            {
                MANGOS_ASSERT(count == ItemUse::Arity);

                WorldObject* subject = ObjectOn(ctx, args[0].AsEntity());
                Player* player = subject ? subject->ToPlayer() : nullptr;
                Item* item = player
                    ? player->GetItemByGuid(ObjectGuid(args[1].AsEntity().guid))
                    : nullptr;
                SpellCastTargets const* targets =
                    Borrowed<SpellCastTargets const>(args[2].AsLent(),
                                                     Domain::CastTargets);
                if (!player || !item || !targets)
                {
                    return Verdict::Continue;
                }

                // Whatever the player aimed it at, which is what a check like
                // "not on something that already has the ointment" is about.
                // Himself when he aimed it at nothing, so a step always has
                // somebody to ask about.
                WorldObject* aimed = targets->getUnitTarget();
                if (!aimed)
                {
                    aimed = player;
                }

                // INLINE, and the polarity is the opposite of every claim in
                // this switch: a sequence that refuses has BLOCKED the item's
                // own spell, which is a refusal rather than a substitute
                // behaviour. That is why the event is cancellable.
                return RunNow(ctx.map, mai::KindItemUse, item->GetEntry(),
                              player, aimed, player->GetObjectGuid(),
                              item->GetObjectGuid())
                           ? Verdict::Cancel
                           : Verdict::Continue;
            }

            case EventId::CoreAuraDummy:
            {
                MANGOS_ASSERT(count == CoreAuraDummy::Arity);

                Aura const* aura =
                    Borrowed<Aura const>(args[0].AsLent(), Domain::Aura);
                if (!aura || !aura->GetTarget())
                {
                    return Verdict::Continue;
                }

                // ONE effect, not every dummy effect the spell has. Seven of
                // the eight scripts this replaces open with
                //
                //     if (pAura->GetEffIndex() != EFFECT_INDEX_0) return true;
                //
                // and the eighth has a single dummy effect, so the check never
                // mattered there. Written as "the FIRST dummy effect" rather
                // than as "index zero" because that is what those seven lines
                // mean: run once per application, not once per effect. A spell
                // whose dummy sits at index 1 gets the same treatment instead
                // of being silently skipped.
                if (aura->GetEffIndex() != FirstDummyEffect(aura->GetSpellProto()))
                {
                    return Verdict::Continue;
                }

                // Only a creature ever gets here -- the core calls this hook
                // for TYPEID_UNIT alone -- which is why every one of the
                // scripts could cast its target to Creature* without asking.
                bool const apply = args[1].AsFlag();

                Start(ctx.map, apply ? mai::KindAuraApply : mai::KindAuraRemove,
                      aura->GetId(),
                      aura->GetCaster() ? aura->GetCaster()
                                        : aura->GetTarget(),
                      aura->GetTarget(),
                      Map::SCRIPT_EXEC_PARAM_NONE);

                // Never claimed. The core discards the answer -- the call is a
                // bare statement at the end of HandleAuraDummy -- so saying
                // "handled" would only be a lie told to nobody.
                return Verdict::Continue;
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

/**
 * What a verb reaches when it starts another sequence.
 *
 * Declared in MaiActor.h beside the Doing a verb is handed, and defined here
 * because this is where the sequence table lives. The direction matters: the
 * verbs must not include the engine, which is what keeps MaiPerform testable
 * with no world at all.
 */
namespace mai
{
    bool StartSequence(Map* map, uint32 kind, uint32 id, WorldObject* source,
                       WorldObject* target, ObjectGuid owner, ObjectGuid item,
                       bool* cancel)
    {
        return scripting::MaiEngine::StartFrom(map, kind, id, source, target,
                                               owner, item, cancel);
    }

    Sequence const* FindSequence(uint32 kind, uint32 id)
    {
        return scripting::MaiEngine::Find(kind, id);
    }
}
