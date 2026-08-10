# ScriptDev3 — the C++ scripts

This is the back end behind `Scripting/sd3/Sd3Engine.cpp`. The adapter above
implements `IEngine`; everything here is what it drives.

## Where this came from

ScriptDev3 was a git submodule at `src/modules/SD3`, built as a static library
called `mangosscript` that linked `game` while `game` linked it back. It is a
scripting engine now, so it lives where the engines live and compiles straight
into `game`. Three things followed from the move and are worth knowing:

- **The sources can be edited.** As a submodule pinned to an upstream commit
  they could not be, which is why the headers the scripts needed used to be
  force-fed to every translation unit from CMake, out of
  `src/shared/Compat/sd3`. That shim is gone; `include/precompiled.h` — which
  all 480 scripts include by name — carries the list instead.
- **There is no separate revision.** The banner used to print an SD3 commit
  hash next to the core's. The scripts share this repository's history now, so
  there is one hash and it answers for both.
- **Only the TBC scripts are here.** The old build file was 32 KB of one glob
  per dungeon, and the reason was not verbosity: the globs were wrapped in
  `MANGOS_EXP` tests, so the directory list was also the statement of which
  scripts belong to which client. Northrend, Maelstrom, Baradin Hold, the
  Scarlet Enclave and the Culling of Stratholme were never compiled for 2.4.3
  and cannot be -- they call `SetPhaseMask`, `MoveJump`, achievement criteria
  and `SPELL_AURA_PHASE`, none of which exist here. They are deleted rather
  than guarded, because this repository is 2.4.3 and nothing else. The
  remaining per-expansion `#if defined(TBC) || defined(WOTLK) ...` inside the
  sources are upstream's and have not been resolved.

The build follows the layout: `game`'s CMakeLists globs this tree recursively,
so a new script directory needs no build edit.

## Layout

- `include/` — the script authors' prelude and the shared helpers
  (`sc_creature`, `sc_gossip`, `sc_grid_searchers`, `sc_instance`).
- `base/` — the reusable AI bases: escort, follower, guard, pet.
- `system/` — the registry: `ScriptDevMgr` holds the bound scripts,
  `ScriptLoader` calls every `AddSC_*`.
- `scripts/` — the scripts themselves, by continent and instance.
