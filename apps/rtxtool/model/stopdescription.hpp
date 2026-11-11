#pragma once

#include <span>
#include <string>
#include <string_view>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/world/frameworld.hpp>

#include "benchrun.hpp"

namespace RtxTool
{
    /// Degrees clockwise from north, in `[0, 360)`, of the way a stand faces. North is +Y and east
    /// is +X, so the arguments come the other way round from the usual `atan2`.
    float bearingOf(const Stand& stand);

    /// Degrees above the horizon, in `[-90, 90]`, of the way a stand faces.
    float climbOf(const Stand& stand);

    /// One line for a person: where `stop` stands, in numbers worth reading rather than
    /// round-tripping, as a `#` comment either file format takes. A run that opened a window prints
    /// this and `describeBlock` where the eye was left, so a place found by flying can be pasted.
    std::string describeSpot(const Stop& stop);

    /// The air's clocks as the `air` field and `--air` take them back: shortest-round-trip numbers,
    /// so what is read is what was written, to the bit, and no spaces, so the command line's is one
    /// argument unquoted.
    std::string describeAir(const Rtx::AirClock& air);

    /// The whole `views.cfg` section for `stop`, ready to paste, under a slug of its name. The whole
    /// section, because a block with no `cell` is one the view file refuses to load; and
    /// shortest-round-trip numbers, because these are read back into the same floats.
    std::string describeBlock(const Stop& stop);

    /// The same place as one `view` command line, as a `#` comment: the cell, the camera, the hour,
    /// the day and the weather, every one of them named, so a frame somebody saw is a frame the
    /// next run draws again. The day too, which the block has no key for and the moons hang on.
    std::string describeCommand(const Stop& stop);

    /// The id a block is written under: the stop's own name, or one made from it, spelt as a view
    /// file's id.
    std::string describeId(const Stop& stop);

    /// The same place as one key of a film: the block, and the day, which a film's key reads and a
    /// view does not. What `view --keys` appends on Home. A condition the block leaves out is the
    /// file's own, which is what a key reads it as.
    std::string describeKey(const Stop& stop);

    /// Where a window stands, whole: the line for a person, the block for the view file and the
    /// command for the next run. What a window prints on the key and again where it was left.
    std::string describeStanding(const Stop& stop);

    /// What the sky is doing, as a window's title says it after the rate: the weather and the
    /// clock, and while one weather crosses into another, which one and how far.
    struct SkyNote
    {
        std::string_view mWeather;

        /// The weather crossing in, or empty while none is. A crossing takes the weather's own
        /// `Transition_Delta` at the clock's speed — a minute for most at the game's own — or
        /// `sTurnSeconds` under a turn, and the title is where a window shows it, because the HUD a
        /// script's message lands on is off unless `--hud` asked for it.
        std::string_view mArriving{};

        /// How far the crossing has come, nought to one.
        float mCrossed = 0.0f;

        float mHour = 0.0f;
    };

    /// `Thunderstorm, 14:32`, or `Clear → Overcast 37%, 14:32` while a crossing runs, written into
    /// `room` and never allocated: a title is written on the frame path. `room` holds the longest
    /// note, `Thunderstorm → Blizzard 100%, 23:59` at thirty-seven bytes, which is asserted.
    std::string_view writeSkyNote(std::span<char> room, const SkyNote& note);

    /// Which upscaler a frame of `profile` passes through, as the window's title names it: `no` where
    /// nothing upscales, and else the one the renderer's choice came to, `fsr` or `dlss`. `profile` is
    /// a renderer's own (`Rtx::Renderer::getProfile`), whose choice is never `auto`.
    std::string_view describeUpscaler(const Rtx::RenderProfile& profile);
}
