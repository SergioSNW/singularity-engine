-- Physics-bridge demo: a pass-through trigger volume. Nothing blocks it (the
-- physics step never separates trigger pairs), so entities move straight
-- through while OnTriggerEnter/Exit report the crossing.

function OnTriggerEnter(other)
    print("Zone: '" .. other.name .. "' ENTERED the trigger (id " .. other.id .. ")")
end

function OnTriggerExit(other)
    print("Zone: '" .. other.name .. "' LEFT the trigger (id " .. other.id .. ")")
end
