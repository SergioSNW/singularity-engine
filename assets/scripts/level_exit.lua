-- Example: attach this script's path to a Trigger-collider entity (Collider
-- section, Type = Trigger) to make it a level exit. Walking the player
-- capsule into it swaps the active scene for NEXT_SCENE -- Application
-- handles a brief fade to black and back around the swap; the script only
-- ever requests it.
--
-- Change NEXT_SCENE to point at a different .scene file to reuse this
-- script for another exit (duplicate the file if you need two exits with
-- two different destinations in the same level). Keep the path under
-- assets/ -- e.g. "assets/scenes/level_2.scene" -- so it survives Export
-- Build, which only copies assets/ (not arbitrary paths elsewhere on disk).
--
-- Health and score carry over into the new scene; the prompt banner and
-- any Won/Lost status reset, since those describe the level being left.
--
-- Checks `other.player.enabled` so this only fires for the actual player --
-- a scene can have other moving/scripted bodies that would otherwise trip
-- the trigger too.

local NEXT_SCENE = "assets/scenes/level_2.scene"

function OnTriggerEnter(other)
    if not other.player.enabled then return end
    Game.LoadScene(NEXT_SCENE)
end
