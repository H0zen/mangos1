# BotsNG — Bots New Generation

> ## ⚠ UNTESTED BRANCH — DO NOT COMPILE
>
> Parked deliberately, to be resumed when the core underneath it is solid.
> Read [What is actually verified](#what-is-actually-verified) before touching
> anything here: one half of this module has been built and tested, the other
> half has never been near a compiler.

A bot engine written from nothing. There is no predecessor to match and no
behaviour to preserve — bots have never worked in this tree — so nothing here
is shaped by what `src/modules/Bots/` does. That module is not a starting
point; it is a warning, and the measurements in [What the old shape
costs](#what-the-old-shape-costs) are why.

## The one rule this module is organised around

**Deciding and doing are separate, and only one of them may touch the world.**

```
decide/    a pure function: snapshot in, intents out.  Links NOTHING.
world/     the two ends that need the server: sensing and executing.
```

That boundary is enforced by the build, not by discipline. `decide/` is its own
CMake target with no dependencies at all — not `game`, not `shared`, not MySQL
— and it configures even when `BUILD_MANGOSD` is off. The day somebody includes
`Player.h` under `decide/`, the library stops building on its own and says so,
instead of the fact surfacing months later as "the bots cannot be tested
without a server running".

## What is actually verified

| | |
|---|---|
| `decide/` | **Built and tested.** clang 19, `-Wall -Wextra`, no warnings. 16 cases in `src/tests/BotDecideTest.cpp`, all green, run against a tree containing the decision core and the test harness and nothing else. |
| `world/` | **Never compiled.** `Squad.{h,cpp}` and `Sense.{h,cpp}` are written; `Executor` and the runner that ties the three phases together are not written at all. The core APIs they call were read out of `src/game/`, but reading a header is not compiling against it. |
| build wiring | `src/CMakeLists.txt` and `src/tests/CMakeLists.txt` add the decision core and its test. `world/` has no CMake target yet, so nothing tries to build it. |

The decision core's test suite has been checked the only way a suite is worth
anything: by making it fail. Deleting the global-cooldown guard from the
arbiter left all sixteen cases green — the case meant to cover it was passing
because a healthstone had already taken the action channel, not because of the
cooldown at all. It was split so that each half has exactly one candidate for
that channel, and the same deletion now turns it red. A test that has never
been seen red is not evidence.

## What is here

* `decide/Percept.h` — everything a bot knows at one instant, as fixed-size
  PODs. No world headers, no pointers into the world: identity is an
  `ObjectGuid`'s raw value, which has nothing to dangle when a target despawns
  between deciding and acting. It also carries the whole feedback path — one
  `Rebuff` slot saying why the last thing was refused, which is what makes a
  retry queue unnecessary rather than merely unfashionable.
* `decide/Intent.h` — what a bot *wants*, as a `std::variant` of six verbs,
  each carrying what it costs. The channel a verb occupies is **derived** from
  the verb rather than declared beside it, so a proposal cannot claim not to
  use the action channel and cast twice per global cooldown.
* `decide/IntentSink.h` — a fixed-capacity, non-allocating place for layers to
  propose into. A full sink keeps the most urgent proposals, not the earliest.
* `decide/Arbiter.{h,cpp}` — one pass that chooses what fits the tick's
  budgets. Ties break by proposal order, via `std::stable_sort`, which is what
  makes an ordered policy mean something.
* `decide/Policy.{h,cpp}` — a layer is a plain function pointer, a policy is an
  ordered list of them, and `Decide` runs the list and arbitrates. A policy
  holds no per-bot state, so one instance serves every bot of a class.
* `decide/layers/Survival.cpp` — the first layer: healthstones, potions, food
  and water. It knows no item ids.
* `world/Squad.{h,cpp}` — a group's roster, built **once per tick** by
  whichever member asks first. The middle that is usually missing: a party of
  five otherwise walks its own member list five times a tick, and the five can
  disagree about who is nearly dead because each looked at a different moment.
* `world/Sense.{h,cpp}` — the snapshot, refreshed **in stages by cause rather
  than by age**. Health every tick; bags only when something says they changed;
  the roster from the squad. No wall-clock cache anywhere, so the same world in
  the same tick yields the same snapshot however recently it was last read.

## Three decisions worth stating

**No synthesized client packets.** A bot here is a server-side character, not a
fake client. Nothing in this module builds a `CMSG_*` and queues it into the
bot's own session for the server to parse back — item use goes through
`Player::CastItemUseSpell`, the same call the opcode handler makes. One path
in, and the anti-cheat and protocol layers are not asked to referee a
conversation the server is having with itself.

**The global cooldown is the bot's own accounting**, and it can be precisely
because of the rule above: every cast goes through one executor, so one place
knows when the last one started and what it cost. `GlobalCooldownMgr` answers
whether a spell is blocked, never for how much longer, so a bot asking the core
cannot pace a rotation. The number comes from the spell's own
`StartRecoveryTime`.

**Item categories were read, not remembered.** Food is category 11, drink 59,
and the healthstone **1153** — which is *not* the potions' category 4. The
healthstone also carries its spell in slot 1, not slot 0, so a classifier that
reads `Spells[0]` finds nothing and the bot dies with a stone in its bag. Both
facts came out of this world's `item_template`.

## What the old shape costs

Measured on `src/modules/Bots/` in this tree, for the record and as the reason
none of it was reused:

| | |
| --- | --- |
| lines / files | 51,086 in 622 |
| `new` expressions | 2,568 (against 12 `nullptr` and 1,288 `NULL`) |
| `AI_VALUE` call sites | 266, each a string concatenation, a `std::map` lookup and a `dynamic_cast` |
| `time(0)` | 81 — the decision clock is wall-clock, to the second, under a 1.5 s global cooldown |
| `Event` | holds a `WorldPacket` **by value** and is passed by value everywhere |

And three defects rather than costs: which strategy supplies an action depends
on the **alphabetical** order of strategy names (`map<string, Strategy*>`
iterated for the first match); `Engine::addStrategies(string first, ...)`
passes `std::string` through varargs; and `PlayerbotAI::CastSpell` deletes a
`Spell` and then calls `cancel()` on it.

## Where to pick this up

1. `world/Executor` — one `std::visit` over the plan, returning an outcome per
   intent so `Senses::Refused` has something to record.
2. `world/BotRunner` — the three-phase tick (sense, decide, execute), named
   that way so that deciding many bots at once is later a flip rather than a
   rewrite. Only phase three touches the world, which is what keeps `mangosd`
   the sole authority over game state.
3. A CMake target for `world/`, built with the module under `BUILD_MANGOSD`,
   linking `game` and `botsng_decide` — never the reverse.
4. Only then: the lifecycle. Who creates a bot, what session it holds, how it
   logs in. None of that exists yet, and none of it should be borrowed.

## Running what does work

```sh
cmake -S . -B ../build -G Ninja -DWITH_TESTS=1
cmake --build ../build --target mangos_tests
../build/src/tests/mangos_tests -only BotDecide
```
