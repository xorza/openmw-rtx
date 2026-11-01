local async = require('openmw.async')
local types = require('openmw.types')
local util = require('openmw.util')
local world = require('openmw.world')
local I = require('openmw.interfaces')
local spellCasting = require('scripts.omw.spellcasting.common')
local surfaceLight = require('scripts.omw.surfacelight.common')

-- The light record of each radius, made once: a record made for each cast would stay in the save
-- for good after its lamp went out. White, as the glow the effect gives an actor is.
local records = {}

local function recordOf(radius)
    local id = records[radius]
    if not id then
        id = world.createRecord(types.Light.createRecordDraft({
            radius = radius,
            color = util.color.rgb(1, 1, 1),
        })).id
        records[radius] = id
    end
    return id
end

local putOut = async:registerTimerCallback('SurfaceLightOut', function(lamp)
    if lamp:isValid() and lamp.count > 0 then
        lamp:remove()
    end
end)

-- An actor the bolt can affect takes its Light (`common.inflict`). Anything else, a lock included,
-- takes no Light, so the light stays on the surface struck.
local function takesLight(object)
    return spellCasting.targetIsValid(object) and types.Actor.objectIsInstance(object)
end

I.Projectiles.addOnProjectileHitHandler(I.Projectiles.TYPES.Magic, function(projectile, hitResult)
    if not hitResult.hit or takesLight(hitResult.hitObject) then
        return
    end
    local record = spellCasting.getMagicRecord(projectile.userData.id)
    if not record then
        return
    end
    local lamps = surfaceLight.lamps(record.effects, math.random)
    if #lamps == 0 then
        return
    end
    -- A bolt flies only in the scene the player stands in. The cell is named now, as the player
    -- may go through a door before the measure comes back.
    local player = world.players[1]
    player:sendEvent('SurfaceLightMeasure', {
        cell = player.cell.id,
        hitPos = hitResult.hitPos,
        hitNormal = hitResult.hitNormal,
        lamps = lamps,
    })
end)

return {
    eventHandlers = {
        SurfaceLightStand = function(data)
            local cell = world.getCellById(data.cell)
            for _, lamp in ipairs(data.lamps) do
                local object = world.createObject(recordOf(lamp.radius))
                object:teleport(cell, lamp.position)
                async:newSimulationTimer(lamp.duration, putOut, object)
            end
        end,
    },
    engineHandlers = {
        onSave = function()
            return { records = records }
        end,
        onLoad = function(data)
            records = data and data.records or {}
        end,
    },
}
