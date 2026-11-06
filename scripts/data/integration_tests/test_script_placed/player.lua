local testing = require('testing_util')
local matchers = require('matchers')
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local types = require('openmw.types')
local util = require('openmw.util')

local from = util.vector3(4096, 4096, 1746.27)
local to = util.vector3(4500, 4500, 1745.95)

local function castRay()
    return nearby.castNavigationRay(from, to, {
        agentBounds = types.Actor.getPathfindingAgentBounds(self),
        includeFlags = nearby.NAVIGATOR_FLAGS.Walk,
    })
end

local function isClear(hit)
    return hit ~= nil and (hit - to):length() < 1
end

-- The mesh changes on its own thread, after a new game as after a placed object. At the frame limit
-- this waits ten seconds.
local function castRayUntil(isDone)
    local hit = castRay()
    local frame = 0
    while frame < 600 and not isDone(hit) do
        coroutine.yield()
        frame = frame + 1
        hit = castRay()
    end
    return hit
end

testing.registerLocalTestStep('the navigation ray is clear', function()
    testing.expectThat(castRayUntil(isClear), matchers.closeToVector(to, 0.1))
end)

testing.registerLocalTestStep('the navigation ray stops at the pillar', function(pillar)
    local hit = castRayUntil(function(hit)
        return not isClear(hit)
    end)

    -- The ray runs along x = y and stops where the agent's half extent meets the pillar's 46.9. The mesh
    -- is voxels of "cell size" / "recast scale factor" = 0.2 / 0.0294 = 6.8 units.
    local reach = 46.9 + types.Actor.getPathfindingAgentBounds(self).halfExtents.x
    testing.expectEqualWithDelta(hit.x, pillar.x - reach, 6.8, 'the ray stops at the pillar, x')
    testing.expectEqualWithDelta(hit.y, pillar.y - reach, 6.8, 'the ray stops at the pillar, y')
end)

return {
    engineHandlers = {
        onFrame = testing.updateLocal,
    },
    eventHandlers = testing.localEventHandlers,
}
