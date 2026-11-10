-- place_on_request.omwaddon holds one start script, as Lua can run no place instruction of its own:
--
--     Begin Place_On_Request
--     short place
--     if ( place == 1 )
--         PlaceAtPC "HubTemple_Pillar01" 1 0 0
--         StopScript Place_On_Request
--     endif
--     End

local testing = require('testing_util')
local types = require('openmw.types')
local util = require('openmw.util')
local world = require('openmw.world')

local function findPlacedPillar(cell, position)
    for _, object in ipairs(cell:getAll(types.Static)) do
        if object.recordId == 'hubtemple_pillar01' and (object.position - position):length() < 100 then
            return object
        end
    end
    return nil
end

local start = util.vector3(4096, 4096, 1745)
local midway = util.vector3(4298, 4298, 1746)

-- The pillar stands on the ray's line, placed where the player stands. A placed object gets its physics only when
-- something adds the postponed objects, so until then neither the physics nor the mesh has it.
local function placePillar(player)
    player:teleport('', start)
    coroutine.yield()
    testing.runLocalTest(player, 'the navigation ray is clear')

    player:teleport('', midway)
    coroutine.yield()
    world.mwscript.getGlobalScript('Place_On_Request', player).variables.place = 1
    coroutine.yield()
    coroutine.yield()
    local pillar = findPlacedPillar(player.cell, midway)
    testing.expectEqual(pillar ~= nil, true, 'the script placed a pillar')
    return pillar
end

-- With no move of the player after the pillar is placed: any teleport of the player, one within the cell
-- included, changes the cell grid, which adds the postponed objects before the disable could.
testing.registerGlobalTest('a script placed object joins the navigation mesh when an object is disabled', function()
    local player = world.players[1]
    local other = world.createObject('HubTemple_Pillar01')
    other:teleport('', util.vector3(3700, 3700, 1745))
    coroutine.yield()

    local pillar = placePillar(player)
    testing.runLocalTest(player, 'the navigation ray is clear')

    other.enabled = false
    coroutine.yield()

    testing.runLocalTest(player, 'the navigation ray stops at the pillar', pillar.position)
end)

testing.registerGlobalTest('a script placed object joins the navigation mesh when the cell grid changes', function()
    local player = world.players[1]
    local pillar = placePillar(player)
    testing.runLocalTest(player, 'the navigation ray is clear')

    -- One cell over, which keeps the pillar's cell active.
    player:teleport('', util.vector3(12288, 4096, 1745))
    coroutine.yield()
    testing.expectEqual(player.cell.gridX, 1, 'the player moved one cell')

    testing.runLocalTest(player, 'the navigation ray stops at the pillar', pillar.position)
end)

return {
    engineHandlers = {
        onUpdate = testing.updateGlobal,
    },
    eventHandlers = testing.globalEventHandlers,
}
