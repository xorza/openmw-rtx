local core = require('openmw.core')

local surfaceLight = {}

-- A point of Light magnitude is a foot of radius, at the 22 units vanilla rounds a foot to, as the
-- glow the effect gives an actor (`MWRender::Animation::setLightEffect`).
local unitsPerFoot = 22

-- The lamp's radius is the reach the rasterizer draws the actor's glow to, three times the glow's
-- own (`setLightEffect`'s cutoff multiplier).
local reach = 3

-- The ray tracer takes no shadow from what stands within a quarter of a lamp's radius of it
-- (`Rtx::makeLight`'s fitting), so the lamp stands that far off the surface struck: any closer and
-- the wall it stands on would stop none of its light, and it would light the room behind.
local clearanceFraction = 1 / 4

-- The magnitude the engine gives an effect (`roll` in `spelleffects.cpp`): the minimum where the range is
-- empty, and otherwise each whole number from the minimum to the maximum alike.
local function magnitudeOf(effect, roll)
    if effect.magnitudeMax <= effect.magnitudeMin then
        return effect.magnitudeMin
    end
    return roll(effect.magnitudeMin, effect.magnitudeMax)
end

--- The lamps a magic bolt's effects leave on a surface it struck, where no actor took them: one for
-- each Light effect on Target, three times the radius the same magnitude gives an actor's glow, for
-- the effect's duration, and how far off the surface it would stand where nothing is in the way. An
-- effect of no magnitude or no duration lights nothing, as on an actor. `roll(min, max)` draws a
-- whole number from `min` to `max`.
function surfaceLight.lamps(effects, roll)
    local lamps = {}
    for _, effect in ipairs(effects) do
        if effect.id == core.magic.EFFECT_TYPE.Light and effect.range == core.magic.RANGE.Target then
            local magnitude = magnitudeOf(effect, roll)
            if magnitude > 0 and effect.duration > 0 then
                local radius = magnitude * unitsPerFoot * reach
                lamps[#lamps + 1] = {
                    radius = radius,
                    standOff = radius * clearanceFraction,
                    duration = effect.duration,
                }
            end
        end
    end
    return lamps
end

--- Where a lamp stands off the surface struck at `hitPos`, along its normal: `standOff` away, or
-- midway to the first obstacle `free` units along the normal where that is nearer than twice as far,
-- so the lamp stands as far from the surface behind it as from the one it was cast at. `free` is nil
-- where nothing stands within twice `standOff`.
function surfaceLight.stand(hitPos, hitNormal, standOff, free)
    local distance = standOff
    if free then
        distance = math.min(standOff, free / 2)
    end
    return hitPos + hitNormal * distance
end

return surfaceLight
