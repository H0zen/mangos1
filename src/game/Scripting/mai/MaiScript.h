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
#include "combat/pure/ProcPoints.h"

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

    /// How deep loops may nest. Four for the same reason eight states are
    /// eight: each level costs a counter on EVERY running frame, the slot is
    /// handed out at LOAD from the static nesting depth rather than pushed at
    /// run time, and an encounter wanting a fifth is an encounter that wants
    /// writing rather than declaring.
    enum : std::size_t { MaxLoopDepth = 4 };

    /// The most steps one frame may run in one tick. A loop whose body takes
    /// no time would otherwise spin here for ever, and "here" is the world
    /// thread: mangosd has one, and a `while` in a table row must not be able
    /// to stop it.
    ///
    /// Running out is not an error and does not end the sequence -- the frame
    /// simply resumes on the next tick, which turns a runaway loop into a
    /// creature that is busy rather than a server that is gone.
    enum : uint32 { MaxStepsPerTick = 256 };

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
        GuardTargetAura,

        /**
         * The creature's phase NUMBER -- `Actor::phases.current`, the thing
         * `set_phase` writes.
         *
         * Here because it was the one piece of a creature's state that could
         * be written and not read, and the gap was invisible: `phase` is a
         * perfectly good name for a remembered number, so `phase=2` parsed,
         * loaded, and quietly compared against a state slot that `set_phase`
         * has never touched. It read zero for ever. The example in schema.sql
         * and in the manual was that guard.
         *
         * A rule's `phase_mask` says which phases it does not fire in, which
         * is the same question asked once per rule and inverted. This is it
         * asked per step, in the words a person would use.
         *
         * The name is reserved -- see RuleSet::Reserved -- so a creature
         * cannot also have a state called `phase` and two things cannot mean
         * one word.
         */
        GuardPhase
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
     * How a step moves the frame it is standing in -- the whole of the
     * control flow, after the block verbs have been compiled away.
     *
     * THE THREE STRUCTURES, and no more than three. A sequence was already the
     * first: steps in order, each with a time. What the table could not say was
     * the other two, and each of them was being faked somewhere -- `chance` is
     * a probabilistic `if`, `select_else` is an if-else about a target,
     * `random_step` is a switch, `retry` is a do-while, and a sequence that
     * starts itself is a loop with no exit condition. Six ad-hoc decisions in
     * six columns, each with its own meaning to remember.
     *
     * This is those two structures written down once. `if`/`else`/`end`,
     * `repeat n`/`end` and `while`/`end` are what an author writes in the
     * `action` column; MaiCompile turns them into these five, resolves every
     * jump to an index, and refuses a script whose blocks do not balance --
     * at LOAD, which is the whole argument for MAI being data.
     *
     * DELIBERATELY NOT MORE. There is no call, no return, no expression, no
     * `or`. A guard is still one comparison against one remembered number and
     * a rule may carry several. The line MaiScript has drawn since the first
     * commit is unchanged: when an encounter needs more than this, it is a
     * program and belongs in C++ -- a boundary worth keeping visible rather
     * than eroding one operator at a time.
     */
    enum FlowKind : uint8
    {
        /// An ordinary step. It runs, the frame advances by one.
        FlowNone,

        /// `if` and `while`: when the guards do NOT hold, go to @a jump
        /// instead of to the next step. When they do, fall through.
        FlowSkip,

        /// `else`, `break` and `continue`: go to @a jump, always.
        FlowAlways,

        /// `repeat`: load the loop counter and fall through, or -- when the
        /// count is zero -- go to @a jump, which is past the matching `end`.
        FlowEnter,

        /// The `end` of a `repeat`: count down, and go back to @a jump while
        /// anything is left. Falls through on the last turn.
        FlowLoop
    };

    /// A step that does not move the frame. 0xFFFF rather than 0, because 0 is
    /// a perfectly good jump target: the first step of the sequence.
    enum : uint16 { NoJump = 0xFFFF };

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
         * What must hold for this step to run at all, as a window into the
         * sequence's own guard table.
         *
         * INDICES RATHER THAN A VECTOR, and that is the whole reason the table
         * is on the Sequence. A step is a fixed-size record and an array of
         * them is contiguous -- which is why walking a handful of them looking
         * for the ones due is not a pointer chase. A `std::vector<Guard>` per
         * step would have put a heap allocation and an indirection on all
         * 27,561 converted steps to buy something 27,561 of them do not use.
         *
         * A step with none -- every step in the world today -- runs as it
         * always did, and pays four bytes it never reads.
         */
        uint16   guardFirst = 0;
        uint8    guardCount = 0;

        /// How this step moves the frame. FlowNone on everything the world
        /// currently has; the rest is what MaiCompile writes.
        FlowKind flow = FlowNone;

        /// Where it moves it TO, as an index into the same sequence. NoJump on
        /// an ordinary step -- and on a guarded ordinary step, which is simply
        /// skipped when its guards fail rather than jumping anywhere.
        uint16   jump = NoJump;

        /// Which of the frame's loop counters a `repeat` and its `end` share.
        ///
        /// Handed out at LOAD from the static nesting depth, not pushed and
        /// popped at run time, which is what lets `break` be an ordinary jump:
        /// there is no stack to unwind on the way out, because there is no
        /// stack. Nesting deeper than MaxLoopDepth is refused where it can be
        /// seen -- when the script loads.
        uint8    loopSlot = 0;

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
        std::vector<Step> steps;    ///< sorted by atMs unless @a program

        /**
         * Every guard any of the steps carries, in one run.
         *
         * A step names a window into this rather than owning a vector, so that
         * a step stays a fixed-size record -- see Step::guardFirst. Empty on
         * every sequence converted from `dbscripts_on_*`, which is all of them
         * today.
         */
        std::vector<Guard> guards;

        /**
         * Whether the steps are a TIMELINE or a PROGRAM, decided at load by
         * whether anything in them branches.
         *
         * The distinction is not cosmetic and it is why the two can share one
         * runner. A timeline is sorted by `at_ms` and read in that order, which
         * is what makes it printable without being run and is what the 27,561
         * converted steps are. A program is read in `seq` order -- sorting it
         * would reorder the branches away from the blocks they belong to --
         * and its `at_ms` still gate each step against the same clock.
         *
         * So a script with no control verb in it behaves EXACTLY as it did
         * before this existed, down to the sort. Nothing had to be reconverted
         * to keep working, which is the only way a change to a live table's
         * meaning is safe to make.
         */
        bool              program = false;

        /**
         * The last moment anything happens, which is how long a frame must be
         * kept alive.
         *
         * A timeline is sorted, so that is its last step. A program is not,
         * so it is the largest time in it -- and for one containing a loop
         * that is a floor rather than an answer: how long a `while` runs is
         * the question a `while` exists to leave open.
         */
        uint32 Duration() const
        {
            if (steps.empty())
            {
                return 0;
            }

            if (!program)
            {
                return steps.back().atMs;
            }

            uint32 most = 0;
            for (Step const& step : steps)
            {
                if (step.atMs > most)
                {
                    most = step.atMs;
                }
            }
            return most;
        }
    };

    /**
     * Phases, still a bitmask.
     *
     * EventAI's whole notion of state, and it deserves to become named states
     * -- `state combat`, `state frenzy` -- which is what the design says and
     * what a conversion cannot deliver in the same change that moves twenty
     * thousand rows. A rule carries the mask of phases it does NOT fire in,
     * inverted, exactly as the tables have it, so a converted row means what
     * it meant.
     */
    struct Phases
    {
        uint32 current = 0;     ///< the phase NUMBER, 0..31; ZERO is where a
                                ///< creature starts, as EventAI had it -- and
                                ///< a creature that has died is put back to it

        bool Allows(uint32 inversePhaseMask) const
        {
            return inversePhaseMask == 0 ||
                   (inversePhaseMask & (1u << current)) == 0;
        }
    };

    /**
     * One creature's MAI state.
     *
     * Deliberately POD-ish and free of pointers to the world: it is owned by
     * the AI object, which is owned by the creature, so anything it held would
     * have the creature's lifetime anyway -- and a guid is what survives the
     * creature dying mid-sequence.
     *
     * IN THE MODEL HEADER, not beside the verbs, because a FRAME can carry one
     * -- see Frame::actor. Being POD is what makes that possible: the copy a
     * dying creature hands on is a copy, with nothing in it that can dangle.
     */
    struct Actor
    {
        Phases phases;

        /// What this creature remembers. Named in the tables and numbered
        /// here: the names are interned per creature ENTRY when its rules load,
        /// so a guard costs an array index at run time and still reads as
        /// `enraged=0` where a person looks at it.
        ///
        /// Cleared by Reset, which is what makes "already enraged" mean
        /// already enraged IN THIS FIGHT.
        uint32 states[MaxStates] = {};

        /// Whoever this creature was told to remember, for the steps that come
        /// after the one that chose. A guid rather than a pointer for the
        /// reason everything here is: the fight outlives the choosing, and
        /// whoever was picked can die between two beats of it.
        ///
        /// Cleared by Reset, so a focus does not survive a wipe.
        ObjectGuid remembered;

        /// Health this creature refuses to drop below, and whether the number
        /// is a percentage. Zero means it dies like anything else.
        uint32 invincibilityHp = 0;
        bool   invincibilityIsPercent = false;

        /// Which AI events this creature will pass on. EventAI wrote it as a
        /// mask and so does this.
        uint32 throwMask = 0;

        /// Whether the AI drives movement and melee at all. A scripted
        /// encounter turns these off while it choreographs something and back
        /// on afterwards, and forgetting the second half is the commonest way
        /// a boss ends up standing still for ever.
        bool combatMovement = true;
        bool meleeAllowed = true;

        void Reset()
        {
            phases = Phases();
            remembered.Clear();
            for (uint32& state : states)
            {
                state = 0;
            }
            invincibilityHp = 0;
            invincibilityIsPercent = false;
            throwMask = 0;
            combatMovement = true;
            meleeAllowed = true;
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

        /**
         * The numbers the moment that started this sequence carried.
         *
         * A SNAPSHOT, and that is the point rather than the cost. "Fifteen
         * percent of the damage that triggered this" is fifteen percent of
         * the damage that triggered it, not of whatever the number would be
         * three seconds later when a delayed step gets to run -- and the aura
         * that supplied half of them may not exist by then.
         *
         * Zero for every sequence not started by something that carries
         * numbers, which is most of them.
         */
        Combat::PointsInputs numbers;

        /**
         * Which LOADING of the shared sequence table @a sequence points into.
         *
         * Zero means it does not point into it at all -- a rule's own steps,
         * which live in the rule and are never reloaded. Anything else is the
         * value `mai::SequenceStamp()` had when the frame was made, and a
         * frame whose stamp is not the current one is holding a pointer to a
         * sequence that has been freed.
         *
         * A NUMBER RATHER THAN A REGISTRY, and that is the whole reason it
         * works. `.reload mai_script` rebuilds the table and frees every
         * Sequence in it. The engine drops its own frames, but a creature that
         * started a branch holds one of those pointers on its AI object, and
         * the engine has no list of live AI objects to go and tell. Comparing
         * two integers answers "is this still good" WITHOUT dereferencing the
         * pointer -- which is the only question that can still be asked safely
         * once the answer might be no.
         */
        uint32          stamp = 0;

        /**
         * The creature's own state, when this frame was HANDED ON by one.
         *
         * A death sequence is the case, and it is the case that could not be
         * written at all before. `Creature::Update` stops calling the AI the
         * moment a creature is not alive, so the steps of a `died` rule that
         * have a time on them had nobody left to tick them: they were queued
         * on an AI object that would never run again, and the Reset that
         * follows death threw them away. "Say this three seconds after I die"
         * was not expressible as a creature rule.
         *
         * It is now, by moving the frame rather than the clock: what is left
         * of the sequence is handed to the MAP, which ticks whatever happens
         * to the creature. And what a map frame does not have is the creature
         * -- so the creature's state comes with it, by value.
         *
         * BY VALUE AND AS A COPY, both deliberate. The AI object is destroyed
         * with the creature, so a pointer would dangle within the second; and
         * what a death script wants to ask about is the state AT THE MOMENT OF
         * DEATH -- "did it enrage before it went down" -- which is a snapshot
         * and not a live reading. Fifty-six bytes on a frame, and the frames
         * a busy instance has are counted in hundreds.
         *
         * @a hasActor rather than a pointer or an optional, because a Frame is
         * copied on every tick of every sequence and must stay trivial.
         */
        Actor           actor;
        bool            hasActor = false;

        /**
         * How many turns each open `repeat` has left.
         *
         * On the FRAME rather than the sequence, for the reason everything
         * else here is: the sequence is shared by every run of it, and two
         * creatures three turns apart in the same loop are two frames. The
         * slot is fixed at load from the nesting depth, so this is an array
         * and not a stack -- see Step::loopSlot.
         */
        uint32          loopCount[MaxLoopDepth] = {};

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
