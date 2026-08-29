-- Example: attach this script's path to any entity with a Trigger collider
-- (Collider section, Type = Trigger). Walking the player capsule into it
-- adds 10 points and removes the pickup from the scene.
--
-- entity:Destroy() queues the entity for removal -- it doesn't disappear
-- instantly (the actual removal happens once, after this frame's physics/
-- script passes finish, since destroying it mid-callback would corrupt
-- whatever loop just called into this script), so the `collected` flag
-- still matters: it stops a second overlap in that same frame from
-- scoring twice before the removal takes effect.

local collected = false

function OnTriggerEnter(other)
    if not other.player.enabled or collected then return end
    collected = true
    Game.AddScore(10)
    Game.ShowPrompt("+10 points!")
    entity:Destroy()
end
