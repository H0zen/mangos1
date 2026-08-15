# MAI

The scripting engine in `src/game/Scripting/mai/`. It is what drives creatures
that name it in `creature_template.AIName`, and it is what runs every sequence
the world starts — a quest handed in, a spell landing, a gameobject used, a
gossip option chosen.

It replaced two engines that were each half of it: **EventAI**
(`creature_ai_scripts` — a table of triggers with three action slots and no way
to say "then") and the **DB scripts** (`dbscripts_on_*` — ten tables of timed
command lists with no way to say "only if"). Both engines and both sets of
tables are gone; those rows live in MAI's tables now.

Two things they left behind, and both are load-bearing: `src/game/Scripting/dbscripts/`
is still the **body** of every verb MAI has not yet rewritten natively (see
"Still scaffolding"), and `Map::m_scriptSchedule` still exists for delayed
commands the core generates itself. MAI does not use that queue; other code
still writes it.

**ScriptDev3 is still here** (`src/game/Scripting/sd3/`, 480 C++ scripts) and is
not going anywhere by itself. What MAI aims at is the reason many of those
scripts had to be C++ at all — `if (!m_bEnraged && health < 26%)` is a health
trigger *and* a memory, and neither old table could hold the second half.

---

## The one idea

**A rule says WHEN. A sequence says WHAT, and WHEN counted from its own start.**

That is the whole of what the two old systems were each missing, and each of
them spent real effort faking the other:

- EventAI faked sequences with timers. A creature that says one line, waits
  three seconds and says another was three rows, a phase field and two timers,
  because there was nowhere to write "then".
- The DB scripts faked rules by having ten tables. `dbscripts_on_quest_end`
  *is* a rule — "when a quest ends" — expressed as a table name, and unable to
  carry a condition or a chance.

Fold them together and you get one model: a **trigger** with a **guard**, and
a **sequence of steps with times**. Everything else in MAI follows from that.

---

## The tables

Five, where there were thirteen. DDL and the reasoning behind every column are
in [`schema.sql`](src/game/Scripting/mai/schema.sql).

| table | one row is |
| --- | --- |
| `mai_script` | a sequence the world can start: its `kind`, its id, its name |
| `mai_step` | one thing that happens in such a sequence |
| `mai_rule` | when a creature starts a sequence: trigger, guard, chance, phases |
| `mai_rule_step` | one thing that happens in a rule's sequence |
| `mai_text` | everything anything says — the union of the three old text tables |

`kind` is one of fifteen: the nine `dbscripts_on_*` types by name, plus
`internal`, `aura_apply`, `aura_remove`, `branch`, `item_use` and
`areatrigger`. What the `id` means depends on the kind — a quest id, a spell
id, a creature entry, a gameobject guid. That was true before as well; what is
new is that the kind is a **column** rather than a table name, so a change to
the command set is one `ALTER` instead of nine.

`mai_text` is a union with the ids kept verbatim: the three old tables occupied
three disjoint ranges (`-2005..-1`, `-1999926..-1000000`, `2000000001..`) and
still do, so nothing that pointed at a text had to be renumbered. The migration
refuses to merge if they ever collide.

### A step is readable

Parameters are `name=value` pairs in one column, and the names come from the
manifest:

```text
kind   script  seq  at_ms  action                params
spell  11962   0        0  cast_spell            spell=11962 flags=1
spell  11962   1     2000  talk                  text0=-1000123 text1=-1000124
spell  11962   2     2000  temp_summon_creature  entry=2044 despawn_delay=300000
                                                 x=-10953.3 y=988.509 z=98.984 o=5.349
```

That is worse than a column per parameter for a machine and much better for the
person editing it, which is the trade that matters. The old shape was six
columns called `datalong`, `datalong2`, `dataint`..`dataint4`, whose meaning
depended on `command` and was documented in a C++ union nobody editing SQL was
looking at — so a sound id written into a spell slot loaded without a word and
failed once, at run time, in front of a player.

Here every parameter has a **semantic type**, and the loader checks it against
the world: `spell` against the spell store, `creature` against the creature
templates, `quest` against the quests. A script naming something that does not
exist is refused **at load**, with the row named. There is no SQL type for "a
spell that exists"; this is what replaces it.

---

## The vocabulary is declared, not written

Three declaration files, one generator, committed results, and a ctest that
fails when the two drift (`python tools/gen_actions.py --check`):

| declaration | generated | holds |
| --- | --- | --- |
| `actions.manifest` | `MaiActions.gen.h` | 110 verbs, their parameters and types |
| `rules.manifest` | `MaiRules.gen.h` | 45 triggers, their parameters and types |
| `eventai.map` | *(nothing, any more)* | how EventAI's columns map onto them; read only by the SQL conversion, and still checked against `actions.manifest` |

The verb **ids are the DB-script command numbers, unchanged**, so an existing
`dbscripts_on_*` row lowers to the same verb by number and the migration is
mechanical. Holes in the numbering are retired commands and are never filled in.

The loader validates against the generated table, the text parser reads
parameter names from it, and the lowering from `dbscripts_on_*` targets it. All
three would otherwise be three hand-maintained copies of one list.

---

## Control flow

The three structures a program needs. A sequence was always the first. The
other two are seven verbs in the `action` column, with their condition in the
`guard` column:

```text
seq  at_ms  action    params           guard
  0      0  if                         phase=2
  1      0    talk    text0=-1000123
  2      0  else
  3      0    talk    text0=-1000124
  4      0  end
```

`if`, `else`, `end`, `repeat times=N`, `while`, `break`, `continue`. One `end`
closes whichever block is open. There is no `else if` — nest a second `if`,
which is what an `else if` is anyway.

**A condition is not an argument of a verb**, which is why it is not in
`params`, and why a step that merely wants to be conditional needs no block
around it: a guard on an ordinary row is `if` for one line.

### Resolved at load

[`MaiCompile.cpp`](src/game/Scripting/mai/MaiCompile.cpp) turns the blocks into
jumps in one pass with a stack, and **refuses — naming the row —**

- an `if` that is never closed, an `else` with no `if`, a second `else`
- a `break` or `continue` with no loop around it
- `repeat`s nested deeper than `MaxLoopDepth` (4) — only `repeat` takes a
  counter slot, so nesting `while`s is not limited by this and needs no slot
- time that runs backwards along an edge that is not a loop's way back
- a guard on `else` or `end`
- a program longer than 65534 steps, since a jump is a `uint16`

The **loader** — not the compiler, which never sees the flag — additionally
refuses a rule that both branches and carries `random_step`, and a rule step
that names both a buddy and a target selector.

A script that fails to compile is refused **whole**. Half a program is not a
smaller program: an `if` whose `end` is missing would run its body
unconditionally, which is worse than the script not running at all.

### Timeline or program

Decided per script, by whether anything in it branches:

- **No control verb** → a *timeline*. Sorted by `at_ms`, read in that order.
  This is what every converted script is, and it is read exactly as it was
  before control flow existed — not reordered, not given a jump.
- **Any control verb** → a *program*. Kept in `seq` order, because sorting it
  would move a step out of the block it belongs to. `at_ms` still gates each
  step against the same clock; the compiler checks the times instead of
  rearranging them.

### Time

A jump moves the program counter. What it does to the clock depends on
direction, and there is one rule each:

- **Forward: the clock is untouched.** A block occupies its stretch of the
  timeline whichever arm of it runs, so an `if` whose `then` takes five seconds
  and whose `else` takes none still reaches the step after the block at five
  seconds. That is what `at_ms` already meant.
- **Backward: a new turn**, so the clock comes back with it by the turn's
  **period** — the `end`'s own `at_ms` less the body's first. An author writes
  one turn and gets it repeated:

```text
   0  repeat  times=3
   0    cast_spell  spell=11962
1000  end
```

three casts a second apart. What comes off is the period rather than the head's
time, so the remainder of a long tick survives and a hundred turns do not drift
by a hundred hitches.

### It cannot hang the server

`mangosd` has one world thread. A frame gets `MaxStepsPerTick` (256) steps in
one tick and no more; running out **yields the tick rather than ending the
sequence**, so a loop that is not advancing is a creature that is busy and a
line in the log, not a server that is gone.

An unbounded `while` over a body that takes no time is deliberately **not**
refused at load: it cannot be told apart from the same loop over a guard the
body is about to change.

### What there is not

No goto, no label, no call, no return, no expression, no `or`. Three structures
is what a program needs. An encounter that needs a fourth thing here is a
program, and belongs in C++ — a boundary worth keeping visible rather than
eroding one operator at a time.

---

## Guards and state

A guard is **one comparison against one remembered number**, and a rule or a
step may carry several, all of which must hold:

```text
enraged=0                  fires only while that is still zero
kills>=3 phase!=2          both
instance:6=1               a field of the instance's own data
aura:9438>=3               stacks on the creature; 0 means "not under it"
target_aura:12654=0        the same, of its victim
```

Six comparisons (`=`, `!=`, `<`, `<=`, `>`, `>=`), no `or`, no nesting, no
arithmetic in the comparison itself.

`phase` is **reserved**: it reads `Actor::phases.current`, the number
`set_phase` writes, and a `set_state name=phase` is a load error. Without that
it was an ordinary state name, so `phase=2` compared against a slot nothing
ever wrote — a guard that could not become true, and the one this document and
`schema.sql` both used as their example.

Any other bare name is one of the creature's own remembered numbers. Eight slots
(`MaxStates`), **named in the tables and interned per creature entry when its
rules load** — so a guard costs an array index at run time and still reads as
`enraged=0` where a person looks at it. `set_state` and `add_state` write them;
`Actor::Reset` clears them, which is what makes "already enraged" mean *already
enraged in this fight*.

A sequence the world starts has no creature, so a bare name is refused there at
load; `instance:`, `aura:` and `target_aura:` still work.

**One evaluator, behind one interface.**
[`MaiGuard.h`](src/game/Scripting/mai/MaiGuard.h) declares `Sight` — a single
virtual `Ask` — implemented by the creature's AI and by a small adapter for
world sequences. A rule's guard, a step's guard, and a guard in a sequence with
no creature behind it all go through it. An **unanswerable** question (no
instance, no victim, no creature) fails closed rather than reading as zero,
because zero is a real encounter state.

---

## Targeting

Three separate mechanisms, and they are separate because they answer different
questions.

**The buddy** (`buddy_entry`, `buddy_range`, `buddy_flags`) — the DB scripts'
own: find a creature of this entry within this range, or by guid outright. It
is a property of the **row**, not of the verb: any action at all may be
redirected at a creature found nearby. Declared as a parameter it would have
been repeated on forty-seven verbs and still been wrong about what it changes.

**The selector** (`select`, `select_else`, `select_flags`, `select_source`) —
EventAI's contribution, and the thing the DB scripts genuinely lacked. A queued
command list knows its source and target when it is queued; a creature's AI does
not. "The second name on my threat list" can only be asked at the moment the
step runs. Twelve of them: EventAI's own eleven, numbered as `TARGET_T_*` so a
converted row means what it meant, and `SelectRemembered` — whoever this
creature was told to remember, which is what a focus, a mark and a chain all
need.

**A step uses one or the other, never both.** The buddy and the selector name
the same thing — the third object the four flags rearrange — so a row that
found a buddy keeps it, and a row that says both is refused at load. It did not
used to be: the selector overwrote the buddy on every rule step, and a rule
step's selector defaults to "self", so "detonate one of the adds" found an add
and detonated the boss.

- `select_else` is the fallback ScriptDev writes by hand at almost every
  selection. Without it, a step whose selector found nobody simply does not
  happen — which bites exactly when a group is smallest.
- `select_source` is whom the step acts **as**. The symmetric half, and the
  lever that was missing: "the thing I just summoned attacks a random player"
  needs both halves at once and cannot be said with a swap.

**The four flags** (`MaiTargeting.h`) rearrange source, target and buddy, in
order. Pure logic over three handles, so it is tested with integers and no
world at all.

---

## The runtime

```text
ScriptHost ── IEngine ──> MaiEngine          per process, owns the data
                            │
              ┌─────────────┼──────────────┐
              │             │              │
        m_sequences    m_frames       m_rules
        (per kind+id)  (per Map)      (per creature ENTRY)
                            │              │
                       Runner            MaiCreatureAI  one per creature:
                       (program counter)                timers, phase, states,
                            │                           its own frames
                            └──> Execute ──> PerformNative
                                          └─> borrowed ScriptDev body
```

MAI is one of the engines registered on the seam, not the only one: `ScriptHost`
also holds SD3 and Luau, and `Bid` is how they settle who drives a creature.

**`MaiEngine`** implements the seam's `IEngine`: `Dispatch`, `Bid`,
`MakeCreatureAI`, `LoadData`, `ReloadData`, `Tick`, `RetireState`. It keeps its
own schedule and does not touch `Map::m_scriptSchedule` — `Tick` already arrives
per map with the map's own diff, so there is nothing a shared queue would
provide.

**A `Frame`** is one *run* of a sequence: where it got to, how much time has
passed, and the guids it is acting on. Guids rather than pointers for every
**object it names**, for one reason: a sequence is a thing that happens **over
time**, and anything it names can die in the middle of it. Resolving late means
a dead actor ends the step rather than the process.

That is about the world, not about MAI's own data — `Frame::sequence` and a
rule's `Rule*` are pointers, into tables the engine owns. The rules are never
rebuilt under a live world (a reload of `mai_rule` is refused). The sequences
**are**, and a frame that points into them carries `Frame::stamp`, which says
which loading it points into: a `.reload mai_script` bumps the number, and any
frame still holding the old one is dropped the next time it is looked at,
without following the pointer to find out.

**Two paths run steps, and they are one walk.** Most sequences are queued and
ticked. `item_use` and `areatrigger` must answer the caller *in the same call*
— whether the item's spell may go ahead is not an answer that can arrive next
tick — so they run inline, through a `Runner` with no time passed, which makes
the steps at time zero due and nothing else. Inline nesting is capped at 8, as
the creature AI's is.

**`Runner`** ([`MaiRunner.h`](src/game/Scripting/mai/MaiRunner.h)) is the
machine, and it knows nothing about the world — no map, no creature, no server —
which is what lets the whole of the timing and the control flow be tested to
exhaustion with a table. Four rules it has to get right:

- **A tick is not a step.** A 400ms hitch must run every step that came due
  inside it, in order.
- **Several steps share a time**, and all of them are due together, in `seq`
  order.
- **Time is absolute from the start**, so an uneven tick does not lose its
  remainder and every later step drift by it.
- **A sequence can be ended from inside** (`terminate_script`,
  `terminate_cond`), and the rest of that tick is dropped.

`Frame::next` has always been a program counter, which is why the control flow
lives here: a jump is that field written with a different number. **A step that
branches never reaches `Execute`** — it is resolved in the runner and the caller
is handed only steps that do something. A verb therefore cannot become a branch
by accident, because `MaiPerform` is never given the frame.

**`MaiCreatureAI`** is one creature's worth of state — which rules are armed,
what their timers stand at, the phase, and the sequences currently running. The
rules themselves are shared per **entry**: every Onyxia in the world has the
same rules, and what differs between two of them lives here. It implements three
interfaces, each of them somebody else's question: `CreatureAI` (the world's —
what happened), `Driver` (a verb's — carry this out, since only the AI object
can), and `Sight` (the runner's — does this guard hold).

---

## Loading

`mai_step` and `mai_rule_step` are read first, then their parents, in one query
each rather than one per script — twenty thousand round trips at start-up is a
minute of a server's life spent on something a single scan answers. Per row:

1. **Parse** — the verb by name, its parameters by name and type against the
   manifest. Strict: an unrecognised parameter is *refused*, not ignored,
   because an ignored name is a typo that becomes a step quietly doing less than
   it says.
2. **Guards** — parsed by the same code a rule's guard uses, interned into the
   same creature.
3. **Validate** — ids held up against the world, with the script, the step and
   the parameter named, and every bad step **removed**. Reported and removed
   rather than fatal: a world's tables are edited by people, and refusing to
   start a server over one bad row teaches an administrator to turn the check
   off. A script that *branches* loses all of it instead, because removing one
   row from a program leaves a different program rather than a shorter one.
4. **Sort or compile** — a timeline is sorted by `at_ms`; a program goes through
   `MaiCompile`. After validation, never before: the compiler resolves every
   jump to an index, and a removed row moves them.

What is checked: spells, creatures, gameobjects, items, quests, maps, emotes,
areas, taxi paths, factions, and **texts** against `mai_text` (which `LoadData`
reads first for exactly that reason). What is **not**: sounds and movies, which
have no store this core loads; `buddy_entry`; and the spell id inside an
`aura:` guard.

Built **beside** the live table, not into it — the new set is assembled in full
and then swapped in. Assembled *into* the live one it would have to be cleared
first, and then a reload against a database that has gone away would leave a
world with no scripts at all.

`LoadRules` runs **once**, from the final load phase, and reloading it is
refused outright: live AI objects hold pointers into `m_rules`.

---

## Threading and lifetime

`mangosd` is the sole authority over game state and the world runs as a single
heartbeat loop — but maps update in parallel (`MapUpdateThreads` defaults to 2),
and three rules follow:

- **`m_frames` is locked for the LOOKUP only.** Two maps inserting their first
  frame at the same instant rehash the container under both, and a data race is
  undefined behaviour rather than a lost entry. The *vector* a map gets back
  needs no lock — one thread owns a map — and the reference survives another
  thread's rehash because an `unordered_map` is node-based.
- **A step can move the frame it is standing on.** A `die` that goes through
  `DealDamage` reaches `JustDied`, which resets and would free the frame
  underneath the runner; any step that starts another sequence appends to the
  same vector and reallocates it. Both walks — the creature AI's and the
  engine's — go **by index, on a copy**, and a reset from inside a step *marks*
  frames rather than freeing them. (A `die silent=1` and a `despawn_self` do
  not reach `JustDied` at all, so they mark nothing and the runner walks a
  corpse until the next `Reset`.)
- **A map's frames die with the map**, in `RetireState`. A sequence still
  running when the last player left is a sequence about a world that no longer
  exists.

`died`, `evaded` and `reached_home` run **in the callback**, not from the next
`UpdateAI`: `Creature::Update` stops calling the AI once the creature is not
alive or is walking home, so anything queued there would never run.

That covers the steps at time zero. **Anything with a time on it changes
owner**: a creature that is dead or walking home hands the rest of its frame to
the map, whose schedule ticks regardless of what happened to the creature —
which is exactly what `dbscripts_on_creature_death` has always been, and why
that one kind of death script did work. Two things make it safe and faithful:

- the steps point into `mai_rule`, which is loaded once and never rebuilt, so
  the pointer outlives the corpse by construction;
- **the creature's state travels with the frame**, by value, so a guard on a
  death step still reads the fight it belongs to and `remember_target` still
  names whom it named. What cannot travel is the AI object, so `set_timer` and
  the two movement verbs do nothing on an adopted frame — which is what they
  mean on a corpse.

A `reached_home` rule keeps its frame: that creature is alive, home, and being
ticked. The test in the code is `Creature::Update`'s own, written out rather
than guessed at.

---

## Where things are

| file | what |
| --- | --- |
| `MaiScript.h` | `Step`, `Sequence`, `Frame`, `Guard`, `Selector`, `FlowKind` — the model |
| `MaiRule.h` | `Rule`, `RuleSet`, the state-name interning |
| `MaiActor.h` | one creature's state; `Doing`, the argument every verb gets; `Driver` |
| `MaiRunner.h` | the machine: time, the program counter, the jumps, the fuel |
| `MaiGuard.h` | `Sight`, and the one place a guard is compared |
| `MaiCompile.cpp` | blocks into jumps, and every refusal that names a row |
| `MaiParse.cpp` | `name=value` and the guard language |
| `MaiValidate.cpp` | every id held up against the world |
| `MaiExecute.cpp` | one step, carried out: resolve, find, rearrange, perform |
| `MaiPerform.cpp` | the native verb bodies |
| `MaiSelect.cpp` | a `Selector` into the unit it names, now |
| `MaiTargeting.h` | the four flags, as pure logic |
| `MaiLowering.cpp` | a `dbscripts_on_*` row into a `Step`, and back |
| `MaiCreatureAI.cpp` | the triggers, the timers, the phases, the frames |
| `MaiEngine.cpp` | the seam, the loaders, the map schedule |

Declarations: `actions.manifest`, `rules.manifest`, `eventai.map`, `schema.sql`.

Tools in `tools/`: `gen_actions.py` (the generator, `--check` in CI),
`convert_dbscripts.py` and `convert_eventai.py` (the conversions, one SQL file
per entity so a dungeon can be reviewed and reverted on its own),
`make_migration.py` and `make_consolidated.py` (assembly), `survey_sd3.py`.

---

## Testing

`src/tests/MaiLoweringTest.cpp`, built into `mangos_tests` — **30 cases of the
binary's 214, and none of them needs a world.** That is deliberate and it is
what the whole split is for. What it also means is that everything which *does*
need one is untested: no test constructs a `MaiEngine` or a `MaiCreatureAI`, so
frame lifetime, reload, the inline path and the borrowed bodies are read rather
than exercised.

- **The lowering**, over `db_scripts` exported from a *running* world — 2,076
  rows using 36 of the 47 commands, not a set of examples written to make the
  test pass. Every row lowers, and every field arrives where the manifest says.
- **The differential**: MAI's runner and `Map`'s own schedule, simulated from
  the same tick stream over the same chains, asserted to agree. This is what
  made replacing the DB script engine something other than an act of faith.
- **The runner**: long ticks, shared times, drift, stopping from inside.
- **The compiler and the control flow**: every refusal, both arms of an `if`, a
  guard with no block, a `repeat` that keeps its remainder, a `while` that ends
  when the world changes under it, a `continue` that still counts its turn in a
  `repeat` and still takes the whole turn in a `while`, `phase` being the phase
  and not a state, the fuel, and that no control verb is ever handed out.

Run them with the fixture path set:

```sh
MANGOS_TEST_DATA=<repo>/src/tests/data ./mangos_tests
```

---

## Still scaffolding

Named rather than implied, because both leave when the work behind them does:

- **`Step::origin` and `Sequence::origin`** — the `dbscripts_on_*` row a step
  was lowered from, and the type a *borrowed* body should think it is. MAI owns
  the model, the clock and the targeting; the forty-odd effect bodies it has not
  rewritten yet are reused rather than retyped blind, one verb at a time, with
  the differential test watching. When the last one goes, so do these.
- **Phases are still a bitmask.** They are EventAI's whole notion of state and
  deserve to become named states — but not in the same change that moves twenty
  thousand rows, because a conversion has to be checkable against what it
  converted and "the same, but better" is not checkable.
- **`schema.sql` is `DROP TABLE IF EXISTS`** — a development contract, not a
  migration. Migrations live in the separate `mangosone/database` repo as
  transactional, idempotent `Rel##_##_###_*.sql` files.

Not implemented, and not a fourth structure: **`foreach <selector>`**. It is
sugar over iteration, and its cost is in the targeting, which returns one unit
and would have to return a list of guids re-resolved each turn.

## Known gaps

Not defects with a fix pending — things the model does not currently do, worth
knowing before writing a script that assumes otherwise.

- **Nine `RuleId`s exist that no creature can check** (`quest_started` through
  `event_raised`, 64–72). They are the `dbscripts_on_*` kinds, declared in the
  wrong manifest; a `mai_rule` using one loads, arms, and says so on every look.
- **Phases are still a bitmask** on the rule (`phase_mask`), even though a step
  can now guard on the phase number by name.
- **`chance` on a rule is not clamped to 100** the way a step's is.
- The look interval is **50ms**, not EventAI's 500 — less slop on a
  proximity trigger, more grid searches for the 291 `friendly_*` rules.
