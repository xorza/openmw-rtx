-- An hour of the day as the harness spells it: the nearest minute on a twenty-four hour clock,
-- `RtxTool::describeHour`'s spelling, so what a key says and the window's title agree.
-- `RtxHourSpellingTest` holds the two to one answer at every half minute.
return {
    describe = function(hour)
        local minutes = math.floor(hour * 60 + 0.5) % (24 * 60)
        return string.format('%02d:%02d', math.floor(minutes / 60), minutes % 60)
    end,
}
