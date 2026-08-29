-- Example: attach this script's path to any entity's Script field in the
-- Inspector, on an entity with a Trigger collider (Collider section, Type =
-- Trigger). Walking the player capsule into it loses the game -- useful for
-- lava, spikes, or a bottomless-pit catch volume placed below the level.

function OnTriggerEnter(other)
    Game.ShowPrompt("You fell into the hazard!")
    Game.Lose()
end
