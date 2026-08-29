-- Example: attach this script's path to any entity with a Trigger collider
-- (Collider section, Type = Trigger). Walking the player capsule into it
-- adds 10 points, once.
--
-- There is currently no Lua-facing way to destroy or hide an entity, so
-- this can't make the pickup visually disappear -- instead it uses a local
-- `collected` flag (each script gets its own private state, so this is safe
-- per-instance) to make a second touch a no-op rather than scoring twice.

local collected = false

function OnTriggerEnter(other)
    if not other.player.enabled or collected then return end
    collected = true
    Game.AddScore(10)
    Game.ShowPrompt("+10 points!")
end
