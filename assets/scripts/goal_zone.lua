-- Example: attach this script's path to any entity's Script field in the
-- Inspector, on an entity with a Trigger collider (Collider section, Type =
-- Trigger). Walking the player capsule into it wins the game.
--
-- Game.* is the gameplay state/HUD bridge (see the Environment & Shading
-- Player Controller work): ShowPrompt sets the banner the HUD draws at the
-- bottom of the Play view, Win() switches the HUD to the win screen and
-- freezes player movement until Restart (R) or Stop.

function OnTriggerEnter(other)
    Game.ShowPrompt("You reached the goal!")
    Game.Win()
end
