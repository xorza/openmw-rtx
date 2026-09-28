-- The keys a watched run answers to. A player script, because only one sees a key; it writes
-- nothing to the world, and sends what it saw to `sky.lua`, which does.
local core = require('openmw.core')
local input = require('openmw.input')
local self = require('openmw.self')
local ui = require('openmw.ui')

-- Every key a script turns the world by, named once, and all unbound in the game's defaults. The
-- keys a window answers that are not here are read off SDL by the harness: Home, which prints
-- where the window stands, the session's own note, and the brackets, which step the sky the
-- harness crosses (`RtxTool::SkyKeys`). No script can reach either.
local keys = {
    [input.KEY.Slash] = { event = 'RtxPauseClock' },
    [input.KEY.Comma] = { event = 'RtxSpeedClock', steps = -1 },
    [input.KEY.Period] = { event = 'RtxSpeedClock', steps = 1 },
    [input.KEY.PageUp] = { event = 'RtxAddHours', steps = 1 },
    [input.KEY.PageDown] = { event = 'RtxAddHours', steps = -1 },
}

return {
    engineHandlers = {
        onKeyPress = function(key)
            local bound = keys[key.code]
            if bound then
                core.sendGlobalEvent(bound.event, { player = self, steps = bound.steps })
            end
        end,
    },
    eventHandlers = {
        RtxSay = function(text)
            ui.showMessage(text)
        end,
    },
}
