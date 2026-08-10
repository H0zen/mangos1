--!strict
--
-- A worked example, and the shortest tour of the engine there is.
--
-- Copy this next to the directory named by Luau.ScriptsPath in mangosd.conf
-- (default "scripts/luau") and reload with `.reload luau`. It is NOT loaded
-- from here: this file lives with the engine so it stays true when the event
-- names change, not so it runs on a live server.
--
-- The types come from ../events.d.luau, generated from the same manifest the
-- C++ call sites are generated from. Point luau-lsp at it and the field names
-- below are checked; get one wrong and the editor says so.

-- Every event a script can hear is in EVENT, named <category>_<event>.
OnEvent(EVENT.player_on_login, function(e)
    -- A guid is opaque on purpose: 64 bits will not fit in a Lua number, so
    -- it arrives boxed. Compare it and print it; do not do arithmetic on it.
    print("logged in:", tostring(e.player))
end)

-- An in/out slot is MUTATED, not returned. The engine reads the field back
-- out of the table after the handler returns.
OnEvent(EVENT.player_on_give_xp, function(e)
    e.amount = e.amount * 2
end)

-- The return value is the verdict, and only where the event offers one.
--   nothing -> carry on
--   false   -> refuse the action        (cancellable events only)
--   true    -> the script did it itself (claimable events only)
-- Answering where it is not offered is reported, not ignored.
OnEvent(EVENT.player_on_chat, function(e)
    if string.find(e.msg, "hunter2", 1, true) then
        return false
    end
    return nil
end)

-- A borrow is a pointer the world lent for the duration of one call. It is
-- valid inside the handler and nowhere else; ask before trusting one you did
-- not receive in this call.
OnEvent(EVENT.spell_on_cast, function(e)
    if e.spell:IsLive() then
        -- ... it is safe to use here
    end
end)

-- Scripts do not share globals: each runs on its own sandboxed thread, so
-- this counter belongs to this file, and to this map. Each map has a state of
-- its own, because maps update in parallel and a VM is not thread-safe.
local logins = 0
OnEvent(EVENT.player_on_login, function()
    logins += 1
end)
