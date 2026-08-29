-- Example: attach this script's path to any entity's Script field in the
-- Inspector, on an entity with a Trigger collider (Collider section, Type =
-- Trigger). Walking the player capsule into it loses the game -- useful for
-- lava, spikes, or a bottomless-pit catch volume placed below the level.
--
-- Checks `other.player.enabled` so this only fires for the actual player,
-- not any other trigger-tripping body in the scene.

function OnTriggerEnter(other)
    if not other.player.enabled then return end
    Game.ShowPrompt("You fell into the hazard!")
    Game.Lose()
end
