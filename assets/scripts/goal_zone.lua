-- Example: attach this script's path to any entity's Script field in the
-- Inspector, on an entity with a Trigger collider (Collider section, Type =
-- Trigger). Walking the player capsule into it wins the game.
--
-- Game.* is the gameplay state/HUD bridge: ShowPrompt sets the banner the
-- HUD draws at the bottom of the Play view, Win() switches the HUD to the
-- win screen and freezes player movement until Restart (R) or Stop.
--
-- Checks `other.player.enabled` so this only fires for the actual player --
-- a scene can have other moving/scripted bodies (e.g. a physics-driven
-- Bouncer) that would otherwise trip the trigger too.

function OnTriggerEnter(other)
    if not other.player.enabled then return end
    Game.ShowPrompt("You reached the goal!")
    Game.Win()
end
