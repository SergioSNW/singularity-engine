-- Example: attach this script's path to any entity with a Trigger collider
-- (Collider section, Type = Trigger). Walking the player capsule into it
-- costs 25 health, and loses the game once health runs out. Fires once per
-- entry (walk away and back in for repeated damage) -- there is no
-- Lua-facing "still overlapping" query for continuous per-frame damage.

function OnTriggerEnter(other)
    if not other.player.enabled then return end
    local health = Game.GetHealth() - 25
    Game.SetHealth(health)
    if health <= 0 then
        Game.ShowPrompt("You ran out of health!")
        Game.Lose()
    else
        Game.ShowPrompt("Ouch! -25 health")
    end
end
