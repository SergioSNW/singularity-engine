-- Physics-bridge demo: drives this cube along +X until it presses against a
-- solid wall. Demonstrates the Lua collision/trigger hooks: OnCollisionEnter/Exit
-- fire once when a solid-solid pair starts/stops overlapping, and
-- OnTriggerEnter/Exit fire as it crosses the pass-through trigger zone. Every
-- hook receives the other entity as an argument (fields: name, id, transform).

local speed = 1.2   -- world units per second, +X

function OnStart()
    print("bouncer.lua: '" .. entity.name .. "' started (id " .. entity.id .. ")")
end

function OnUpdate(dt)
    local p = transform.position
    p.x = p.x + speed * dt
end

function OnCollisionEnter(other)
    print("COLLISION ENTER  with '" .. other.name .. "' (id " .. other.id .. ")")
    Audio.Play("assets/audio/beep.wav", 0.8)
end

function OnCollisionExit(other)
    print("COLLISION EXIT   with '" .. other.name .. "' (id " .. other.id .. ")")
end

function OnTriggerEnter(other)
    print("TRIGGER ENTER    with '" .. other.name .. "' (id " .. other.id .. ")")
end

function OnTriggerExit(other)
    print("TRIGGER EXIT     with '" .. other.name .. "' (id " .. other.id .. ")")
end
