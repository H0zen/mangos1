# Luau — the scripting engine an operator writes in

`LuauEngine.cpp` is the adapter; `engine/` is the vendored Luau VM, compiler and
parser, taken from [luau-lang/luau](https://github.com/luau-lang/luau) at
release 0.733 (`ca128af`). Only what is needed to compile and run a script is
here: `Common`, `Ast`, `Bytecode`, `Compiler`, `VM`. The type checker
(`Analysis`) and the JIT (`CodeGen`) are not — the first belongs in an editor,
the second is a decision to take with a profile in hand rather than in advance.

## Why Luau and not Lua

The three that mattered:

- **It is C++17 with CMake**, which is exactly what this tree builds with on all
  three of its toolchains. LuauJIT is C with hand-written assembly and its own
  build; PUC Lua is C and would have needed the same wrapper work for less.
- **It sandboxes for real.** `luaL_sandbox` freezes the globals and every script
  runs on its own sandboxed thread, so one script cannot redefine another's
  world, and none of them can reach the host's table.
- **The manifest already described the events.** `events.d.luau` is generated
  from it beside `ScriptEvents.gen.h`, so the fields a script sees and the
  fields a C++ call site sets come from one file and cannot drift.

## Writing one

```luau
OnEvent(EVENT.player_on_login, function(e)
    print("welcome", tostring(e.player))
end)

-- An in/out slot is mutated, not returned.
OnEvent(EVENT.player_on_give_xp, function(e)
    e.amount = e.amount * 2
end)

-- The RETURN is the verdict: false refuses, true claims, nothing continues.
OnEvent(EVENT.player_on_chat, function(e)
    if string.find(e.msg, "badword") then
        return false
    end
end)
```

Refusing an event that is not cancellable, or claiming one that is not
claimable, is reported rather than ignored — a script that believes it stopped
something it never could is a bug that otherwise never surfaces.

## What crosses, and in what shape

- **Guids are opaque.** An `ObjectGuid` is 64 bits and a Lua number holds 53, so
  passing one as a number would round it silently. They arrive as boxed
  userdata: comparable with `==`, printable with `tostring`, never mangled.
- **Handles** (a quest, a guild, a map) are boxed for the same reason.
- **Borrows** — a `Spell` in flight, an `Aura`, a `WorldPacket` — are valid only
  while the call that lent them is on the stack. Ask before using one across
  anything: `if b:IsLive() then ... end`. A stored borrow read on a later tick
  reports a script error instead of touching freed memory.

## State

One `lua_State` per map, plus one for global-scope events. Maps update in
parallel in this core and a `lua_State` is not thread-safe, so a single shared
VM would be a data race on every event raised from two maps at once. All of
them run the same compiled bytecode: the scripts are one thing, only their live
values are per map. A map going away closes its state.
