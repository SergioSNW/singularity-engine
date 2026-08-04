-- Example gameplay script. Attach it via an entity's ScriptComponent and press
-- Play: the engine binds this file to that entity, then calls OnStart() once
-- and OnUpdate(dt) every frame while playing.
--
-- Available globals (the script's own environment):
--   entity    -> the bound Entity (fields: name, id, transform)
--   transform -> the entity's Transform (position / rotation / scale)
--   self      -> this environment table (store per-instance state here)
--   Vector3   -> vector constructor, e.g. Vector3(1, 2, 3)
-- Lua's standard library (math, string, print, ...) is also visible.
-- Rotation is Euler degrees, matching the editor's Transform panel.

local spin_speed = 60.0   -- degrees per second about the local Y axis
local hover_amp  = 0.0    -- >0 enables a vertical bob in world units

function OnStart()
    print("player.lua: '" .. entity.name .. "' started (id " .. entity.id .. ")")
end

function OnUpdate(dt)
    -- Live-view mutation: the returned vector points straight into the entity,
    -- so modifying its fields writes the transform in place.
    local r = transform.rotation
    r.y = r.y + spin_speed * dt

    if hover_amp > 0.0 then
        local p = transform.position
        p.y = hover_amp * math.abs(math.sin(2.0 * math.pi * 0.25))
    end
end
