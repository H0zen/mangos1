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

#ifndef MANGOS_MAI_SCRIPT_H
#define MANGOS_MAI_SCRIPT_H

#include "MaiActions.gen.h"

#include "ObjectGuid.h"
#include "Platform/Define.h"

#include <string>
#include <vector>

/**
 * What a MAI script IS, once it has been read.
 *
 * A sequence of steps, each an action with a time. Nothing else yet -- rules
 * and state arrive in later phases, and adding them before a sequence
 * demonstrably replaces the DB scripts would be building on an unproven floor.
 */
namespace mai
{
    /**
     * One parameter value.
     *
     * Small and flat on purpose. The manifest already said what type each slot
     * has, so an operand does not need to carry one: the action's spec is the
     * type, and this is only storage. Keeping it a plain 32-bit cell means a
     * step is a fixed-size record, an array of them is contiguous, and reading
     * one is not a pointer chase -- which matters because the commonest thing
     * MAI will ever do is walk a handful of steps looking for the ones due.
     *
     * Float and integer share the cell rather than converting, because a
     * coordinate and a spell id are both here and rounding one into the other
     * is the kind of bug that shows up as a creature standing in the wrong
     * place two years later.
     */
    union Operand
    {
        uint32 u;
        int32  i;
        float  f;
    };

    /// The most parameters any verb takes, facets included. `talk` takes four
    /// texts; `summon_at_target` takes four of its own plus a position.
    /// Checked at load, so a manifest edit that outgrows it fails loudly.
    ///
    /// EIGHT UNTIL IT WAS NOT. A position facet is four slots on its own, so
    /// any verb that takes one had four left -- and `summon_at_target` used
    /// all four the moment it learnt a spawn mode, an order to attack and an
    /// order to face. The alternative was to bundle two named booleans into
    /// one `flags` column, which is precisely the thing this format exists to
    /// stop: `attack=1 face=1` says what `flags=3` does not. Twelve costs
    /// sixteen bytes a step and buys the naming back.
    enum : std::size_t { MaxOperands = 12 };

    /// How much a creature may remember. Eight is not a guess: it is what the
    /// scripts being converted actually use -- a phase, an "already enraged",
    /// a kill count -- and a creature needing a ninth is a creature whose
    /// encounter wants writing rather than declaring.
    enum : std::size_t { MaxStates = 8 };

    /**
     * A decision, which is the one thing MAI had no way to say.
     *
     * A trigger says WHEN. A guard says WHETHER -- and the difference is what
     * kept half of SD3 in C++: `if (!m_bEnraged && health < 26%)` is a health
     * trigger and a memory, and MAI had the first without the second.
     *
     * Deliberately NOT an expression language. It is one comparison against
     * one remembered number, and a rule may carry several which all have to
     * hold. No or, no nesting, no arithmetic. Every script this was written
     * for needs exactly this much, and the moment it needs more the honest
     * answer is that the encounter is a program and belongs in C++ -- which is
     * a boundary worth keeping visible rather than eroding one operator at a
     * time.
     */
    enum Compare : uint8
    {
        CompareEq,      ///< ==
        CompareNe,      ///< !=
        CompareLt,      ///< <
        CompareLe,      ///< <=
        CompareGt,      ///< >
        CompareGe,      ///< >=

        CompareEnd
    };

    /**
     * What a guard is asking about.
     *
     * Instance data is here because leaving it out was an inconsistency rather
     * than a limit: MAI writes it three ways -- set_instance_data, _data64,
     * _data_guid -- and could not read it at all, so "act only while the boss
     * next door is still alive" was the one thing a script could say and a
     * rule could not. That asymmetry, not any depth of expression, is what
     * kept several ScriptDev files in C++.
     */
    enum GuardOf : uint8
    {
        GuardState,     ///< one of the creature's own remembered numbers
        GuardInstance,  ///< a field of the instance's own data

        /// How many stacks of a spell are on the creature, or on its victim.
        /// Zero when the aura is absent, which makes `aura:9438=0` mean "not
        /// under it" and `aura:9438>=3` mean what it says -- one comparison
        /// covering both questions the triggers needed two names for.
        GuardAura,
        GuardTargetAura
    };

    struct Guard
    {
        GuardOf of = GuardState;

        /// A state slot when @a of is GuardState; an instance data field when
        /// it is GuardInstance. One field because it is one question -- which
        /// number -- asked of two different holders.
        uint32  subject = 0;

        Compare op = CompareEq;
        uint32  value = 0;

        bool Holds(uint32 held) const
        {
            switch (op)
            {
            case CompareEq: return held == value;
            case CompareNe: return held != value;
            case CompareLt: return held < value;
            case CompareLe: return held <= value;
            case CompareGt: return held > value;
            case CompareGe: return held >= value;
            default:        return false;
            }
        }
    };

    /**
     * Who a step really acts on, when it is not the source.
     *
     * A DB-script row may redirect ANY command at a creature found near the
     * actor -- by entry within a radius, or by guid outright, and optionally
     * searched from the target rather than the source, or among the dead. It
     * modifies the step rather than being a parameter of the verb, which is
     * why it sits here and not in the manifest: declared there it would have
     * been repeated 47 times and still been wrong about what it changes.
     *
     * An empty entry and guid means "no redirection", which is the common case
     * by a wide margin.
     */
    struct Buddy
    {
        uint32 entry = 0;       ///< creature entry to look for
        uint32 guidOrRadius = 0;///< a guid when ByGuid is set, else a radius
        uint8  flags = 0;       ///< the row's data_flags, kept verbatim

        bool IsEmpty() const { return entry == 0 && guidOrRadius == 0; }
    };

    /**
     * Who a step acts on, chosen from what the creature can see right now.
     *
     * EventAI's contribution to the model, and the one thing it had that the
     * DB scripts genuinely lacked. A queued command list knows its source and
     * its target when it is queued; a creature's AI does not -- "the second
     * name on my threat list" is a question that can only be asked at the
     * moment the action runs, and the answer changes between two ticks.
     *
     * It sits on the Step for exactly the reason Buddy does: it MODIFIES the
     * step rather than being a parameter of the verb. EventAI declared it as a
     * parameter, which is why its `cast` takes three arguments and its
     * `remove_aura` takes the target FIRST -- the same concept, in a different
     * column, per verb. Here it is one field in one place, and the per-verb
     * knowledge of which of EventAI's columns held it lives in the lowering,
     * where it can be read as a table and checked.
     *
     * Numbering is EventAI's own TARGET_T_*, unchanged, so a converted row
     * means what it meant. SelectSelf is 0 and is therefore what a step with
     * no selector at all gets -- which is right: a DB-script step acts as its
     * source, and its source is itself.
     */
    enum Selector : uint8
    {
        SelectSelf              = 0,    ///< the creature running the rule
        SelectVictim            = 1,    ///< highest threat
        SelectSecondAggro       = 2,
        SelectLastAggro         = 3,
        SelectRandom            = 4,
        SelectRandomNotTop      = 5,
        SelectInvoker           = 6,    ///< whoever made the rule fire
        SelectInvokerOwner      = 7,
        SelectRandomPlayer      = 8,
        SelectRandomPlayerNotTop = 9,
        SelectEventSender       = 10,   ///< the creature that threw the AI event

        /// Whoever this creature was told to remember. Numbered after
        /// EventAI's ten because it is not one of them: EventAI could ask
        /// about the threat list and about who caused the event, and had no
        /// way to say "the one from a moment ago".
        ///
        /// That is what a focus, a mark and a chain all need. Shirrak picks a
        /// player, announces them, and then summons at their feet three times
        /// a second apart -- three beats that have to agree about who.
        SelectRemembered        = 11,

        SelectEnd,

        /// Not a selector: the absence of one. `select_else` defaults to this,
        /// and a step whose first choice finds nobody is simply skipped --
        /// which is what every rule did before there was a second choice.
        SelectNone              = 0xFF
    };

    /**
     * One thing that happens, and when.
     *
     * @a atMs is measured from the START of the sequence, not from the step
     * before it -- and that is the decision that lets one representation hold
     * both systems. A `dbscripts_on_*` row carries exactly such an absolute
     * delay, so lowering a row is a copy; a script that says `wait 2s` between
     * two actions is accumulating into the same field. Written as a relative
     * gap instead, every DB row would have to be rewritten on the way in, and
     * a mistake there would be invisible.
     *
     * It also makes the runner trivial and, more to the point, INSPECTABLE: a
     * sequence is a sorted list of (time, action), so what a script will do and
     * when can be printed without running it. The differential test against the
     * DB scripts is built on being able to do exactly that.
     */
    struct Step
    {
        uint32   atMs = 0;
        ActionId action = ActionId::None;
        Buddy    buddy;

        /// Whom to act on, resolved at the moment the step runs. Only a rule
        /// ever sets this; a sequence started by the world has a source and a
        /// target already and leaves it at SelectSelf.
        Selector select = SelectSelf;

        /// Whom to try when the first choice finds nobody. ScriptDev writes
        /// this by hand and constantly:
        ///
        ///     pTarget = SelectAttackingTarget(RANDOM, 1, 0, SELECT_FLAG_PLAYER);
        ///     if (!pTarget) { pTarget = m_creature->getVictim(); }
        ///
        /// Without it a step whose selector found nobody is skipped, which on
        /// a pull with one player means the ability simply does not happen --
        /// a difference that shows up exactly when a group is smallest and
        /// least able to absorb it.
        Selector selectElse = SelectNone;

        /// What the selector will accept: a player, someone with mana, someone
        /// out of melee range. The values are Creature.h's SelectFlags.
        ///
        /// Not folded into Selector because they are orthogonal -- every one
        /// of the eleven selectors can be narrowed by every one of these, and
        /// a combined enum would have been eleven times seven names. Fifty-one
        /// ScriptDev files use them, which is what makes the difference
        /// between "random target" and "random target WITH MANA" worth a
        /// column: Mana Burn on somebody with no mana is a wasted cast every
        /// ten seconds, and nothing reports it.
        uint8 selectFlags = 0;

        /// Whom the step ACTS AS, when that is not the creature whose rule it
        /// is. The symmetric half of `select`, and the lever that was missing:
        /// a selector could always choose whom a step acts ON, and the only
        /// way to change who acts was `ReverseDirection`, which does not
        /// choose -- it swaps.
        ///
        /// "The thing I just summoned attacks a random player" needs both
        /// halves at once and cannot be said with a swap. Every add that comes
        /// up angry is this shape, which is why it earns a column rather than
        /// a trick.
        ///
        /// SelectNone -- the default -- leaves the source alone.
        uint8 selectSource = SelectNone;

        /// Laid out in the order the action's ParamSpec table names them, so
        /// operand `n` is `SpecOf(action)->params[n]`. There is no other
        /// mapping to get wrong.
        Operand  operands[MaxOperands] = {};

        /// Out of 100, and 100 is always. The SAME meaning the rule's chance
        /// has -- zero is never -- so nobody has to remember which of the two
        /// inverts.
        ///
        /// They are different questions and both are needed. A rule's chance
        /// asks whether the whole thing happens, is rolled ONCE and shared, so
        /// three steps do not disagree about it. A step's asks whether this one
        /// line gets said, and Thespia is why: she casts her cloud every time
        /// and comments on it half the time.
        uint8    chance = 100;

        /// Which operands were actually given. An absent optional parameter is
        /// not the same as one set to zero: `despawn_self` with no delay means
        /// "now", and `despawn_self 0` means the same thing only by accident.
        uint16   given = 0;

        /**
         * The row this step was lowered from, while the DB tables still exist.
         *
         * MIGRATION SCAFFOLDING, and deliberately visible as such. MAI owns the
         * model, the clock and the targeting from the start, but the forty-odd
         * effect bodies are already written, already correct against this core,
         * and already the thing a differential test would be comparing against
         * -- so they are reused rather than retyped blind. Rewriting twelve
         * hundred lines of effects with no way to run them is how the last two
         * commits earned their titles.
         *
         * The order is: new spine, old bodies, differential test green, then
         * bodies replaced one verb at a time with the test watching. When the
         * last one goes, so does this pointer.
         */
        void const* origin = nullptr;

        bool Has(std::size_t slot) const
        {
            return (given & (1u << slot)) != 0;
        }
    };

    /**
     * A named list of steps, shared by every run of it.
     *
     * Immutable once loaded. What differs between two creatures running the
     * same sequence is the Frame, not this -- which is what allows a hundred
     * of them to run at once without a hundred copies.
     */
    struct Sequence
    {
        uint32            id = 0;

        /// Which `db_scripts` type this came from. Migration scaffolding, the
        /// twin of Step::origin: the effect bodies still want it, for their
        /// own error messages and for the map's "is this script already
        /// running" test. It leaves with them.
        uint32            origin = 0;

        /// The `kind` column, by the name a person typed into it.
        ///
        /// Not derived from `origin`: three of the fifteen kinds share an
        /// origin with another, because MAI's own kinds borrow the nearest
        /// `dbscripts_on_*` type for the bodies that still expect one. Derived,
        /// an `aura_apply` would report itself as a `spell`.
        ///
        /// A string literal out of the loader's own table, so it outlives
        /// everything that reads it and costs a pointer rather than a copy on
        /// each of 800 sequences.
        char const*       kind = "script";

        std::string       name;     ///< as reported in an error
        std::vector<Step> steps;    ///< sorted by atMs

        /// The last moment anything happens, which is how long a frame must be
        /// kept alive.
        uint32 Duration() const
        {
            return steps.empty() ? 0 : steps.back().atMs;
        }
    };

    /**
     * One RUN of a sequence: where it got to, and what it is acting on.
     *
     * The three guids are the DB scripts' own vocabulary and are kept because
     * they are genuinely three different things: the source is who is doing it,
     * the target is who it is being done to, and the owner is the player whose
     * bags an item source lives in -- the one object that cannot be found from
     * its guid alone.
     *
     * Guids, not pointers, for the reason the seam gives everywhere else: a
     * sequence is a thing that happens OVER TIME, and anything it names can die
     * in the middle of it. Resolving late means a dead actor ends the step
     * rather than the process.
     */
    struct Frame
    {
        Sequence const* sequence = nullptr;
        std::size_t     next = 0;       ///< index of the first step not run
        uint32          elapsedMs = 0;

        ObjectGuid      source;
        ObjectGuid      target;
        ObjectGuid      owner;          ///< the player holding an item source

        /// The item whose use started this, when one did. Carried rather than
        /// resolved because the owner's bags are the only place it can be
        /// found -- and carried on the FRAME because an `item_use` sequence
        /// that runs inline and then queues its later half would otherwise
        /// hand those steps an empty guid and no way to say which item.
        ObjectGuid      item;

        /// The creature that threw the AI event this run was started by, when
        /// one was. A guid like the rest and for the same reason: a sequence
        /// outlives the moment that started it, and the sender can be dead by
        /// the time a step three seconds in asks for it.
        ObjectGuid      sender;

        bool Finished() const
        {
            return !sequence || next >= sequence->steps.size();
        }

        /// When the next step is due, relative to the start.
        uint32 NextDueMs() const
        {
            return Finished() ? 0 : sequence->steps[next].atMs;
        }
    };
}

#endif //MANGOS_MAI_SCRIPT_H
