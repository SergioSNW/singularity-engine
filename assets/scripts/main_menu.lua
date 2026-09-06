-- Example: a script-driven main menu, using Stage 7's UI.* bindings and
-- OnGUI() instead of a 3D scene full of gameplay entities. Attach to any
-- entity in a scene that's otherwise just a Camera + Directional Light --
-- the entity carrying this script never needs a mesh, collider, or player
-- component of its own, it just draws.
--
-- OnGUI() is called once per frame, every frame, while Play is running --
-- unlike OnUpdate(dt), it's specifically when the real ImGui draw list is
-- available, so it's the only place UI.* calls are meaningful. Coordinates
-- are pixels from the top-left of the play viewport; UI.Width()/UI.Height()
-- give the current viewport size so a menu can center itself instead of
-- assuming a fixed resolution.
--
-- Change NEXT_SCENE to point at whatever scene "Start" should load -- the
-- same convention level_exit.lua's NEXT_SCENE constant uses.

local NEXT_SCENE = "assets/scenes/level_1.scene"

function OnGUI()
    local w, h = UI.Width(), UI.Height()

    UI.Text(w * 0.5 - 90, h * 0.35, "SINGULARITY ENGINE", 0.90, 0.90, 0.95, 1.0)
    UI.Text(w * 0.5 - 70, h * 0.35 + 28, "a demo game", 0.65, 0.65, 0.72, 1.0)

    local button_w, button_h = 160, 44
    if UI.Button(w * 0.5 - button_w * 0.5, h * 0.55, button_w, button_h, "Start Game") then
        Game.LoadScene(NEXT_SCENE)
    end
end
