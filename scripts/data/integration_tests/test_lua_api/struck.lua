local core = require('openmw.core')
local self = require('openmw.self')
local types = require('openmw.types')

-- An actor that stands still and takes the hits it is sent as the engine's own, with no combat scripts of a game
-- between, and what a test of crimes asks of it from a global script, which may not change it itself.
self:enableAI(false)

return {
    eventHandlers = {
        Struck = function(attack) types.Actor._onHit(self, attack) end,
        Die = function() types.Actor.stats.dynamic.health(self).current = 0 end,
        Blind = function(magnitude) types.Actor.activeEffects(self):modify(magnitude, core.magic.EFFECT_TYPE.Blind) end,
        Alarm = function(value) types.Actor.stats.ai.alarm(self).base = value end,
    },
}
