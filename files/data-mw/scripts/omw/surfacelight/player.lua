local core = require('openmw.core')
local nearby = require('openmw.nearby')
local surfaceLight = require('scripts.omw.surfacelight.common')

-- Walls, doors and the ground are in a lamp's way. An actor is not, as an actor moves on.
local obstacles = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.Door + nearby.COLLISION_TYPE.HeightMap

-- The ray starts a unit off the surface struck, so that it does not meet the surface it starts on.
local rayStart = 1

-- A global script casts no rays, so the room in front of a struck surface is measured here.
return {
    eventHandlers = {
        SurfaceLightMeasure = function(data)
            local from = data.hitPos + data.hitNormal * rayStart
            for _, lamp in ipairs(data.lamps) do
                local to = data.hitPos + data.hitNormal * (2 * lamp.standOff)
                local obstacle = nearby.castRay(from, to, { collisionType = obstacles })
                local free = obstacle.hit and (obstacle.hitPos - data.hitPos):length() or nil
                lamp.position = surfaceLight.stand(data.hitPos, data.hitNormal, lamp.standOff, free)
            end
            core.sendGlobalEvent('SurfaceLightStand', { cell = data.cell, lamps = data.lamps })
        end,
    },
}
