-- What the keys turn: the game's clock and the hour. A global script, because only one may write
-- the world. Every answer goes back to the player as `RtxSay`. The weather is the harness's own
-- (`RtxTool::SkyKeys`), because what it crosses is a sky no script can reach.
local core = require('openmw.core')
local world = require('openmw.world')

local function say(player, text)
    player:sendEvent('RtxSay', text)
end

-- The game's default `timescale`, which the clock keys halve and double: ×1 is the game's own
-- day, and a crossing of the sky runs at the game's own speed there (`RtxTool::sGameTimeScale`).
-- Bounded at ×1/8, below which the sky stands still to the eye, and ×1024, at which a day passes
-- in under three seconds and the sun is a streak.
local baseScale = 30
local slowest, fastest = -3, 10

-- What the clock ran at before it was paused, so a second press puts it back.
local heldScale = nil

local function pauseClock(player)
    if heldScale then
        world.setGameTimeScale(heldScale)
        say(player, string.format('clock running, ×%g', heldScale / baseScale))
        heldScale = nil
        return
    end

    heldScale = core.getGameTimeScale()
    if heldScale == 0 then
        heldScale = baseScale
    end
    world.setGameTimeScale(0)
    say(player, 'clock paused')
end

-- `steps` powers of two faster, or slower where negative. A paused clock runs again, stepped from
-- where it was held; one the console set between two powers steps to the power on the side
-- pressed, so ×1.5 goes to ×2 and comes down to ×1.
local function speedClock(player, steps)
    local scale = heldScale or core.getGameTimeScale()
    heldScale = nil
    if scale <= 0 then
        scale = baseScale
    end

    -- The power at or below the scale, then the one at or above it, the same where the scale is
    -- a power. The products are exact, so a scale these keys set is found and not approximated.
    local below = slowest
    while below < fastest and baseScale * 2 ^ (below + 1) <= scale do
        below = below + 1
    end
    local above = below
    if baseScale * 2 ^ below < scale then
        above = below + 1
    end

    local exponent = (steps > 0 and below or above) + steps
    exponent = math.max(slowest, math.min(fastest, exponent))

    world.setGameTimeScale(baseScale * 2 ^ exponent)
    say(player, string.format('clock ×%g', 2 ^ exponent))
end

-- The nearest minute, as `Rtx::describeHour` spells it, so this and the window's title agree.
local function describeHour(hour)
    local minutes = math.floor(hour * 60 + 0.5) % (24 * 60)
    return string.format('%02d:%02d', math.floor(minutes / 60), minutes % 60)
end

-- Written as the hour rather than advanced, so the day and the moons stay where they are. Past
-- midnight the engine rolls the day on by itself; before it the day is taken down here, and on
-- day one it stops at midnight, because the engine clamps the day at one.
local function addHours(player, hours)
    local globals = world.mwscript.getGlobalVariables(player)
    local hour = globals.gamehour + hours

    if hour < 0 then
        if globals.day <= 1 then
            hour = 0
        else
            globals.day = globals.day - 1
            hour = hour + 24
        end
    end

    globals.gamehour = hour
    say(player, describeHour(globals.gamehour))
end

return {
    eventHandlers = {
        RtxPauseClock = function(data)
            pauseClock(data.player)
        end,
        RtxSpeedClock = function(data)
            speedClock(data.player, data.steps)
        end,
        RtxAddHours = function(data)
            addHours(data.player, data.steps)
        end,
    },
}
