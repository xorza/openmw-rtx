#pragma once

#include <array>
#include <optional>
#include <string_view>

#include <components/esm/refid.hpp>
#include <components/rtx/frameworld.hpp>

#include "model/benchrun.hpp"

namespace RtxTool
{
    /// Where the run stands and under what sky, as a stop a person could paste: noted every frame,
    /// kept whole with its picture where Home goes down (`HomeKey`), written into the window's
    /// title, and handed back as where the run was left.
    ///
    /// **Taken on a frame that still has a world**, because `OMW::Engine::~Engine` clears the world
    /// before the renderer that holds the run, so what a run ends at cannot be asked of the world
    /// once it has ended.
    class StandingNote
    {
    public:
        /// Starts the note of `stop`: its name, its note and its cell. The eye and the sky are the
        /// frame's, from the next `take`.
        void begin(const Stop& stop);

        /// Notes the frame just drawn: where the eye stood, the sky over it, and `air`, where the
        /// renderer's clocks stood for it.
        ///
        /// **The game's camera and not the traced one**, whose origin is a float inverted out of a
        /// float matrix: a few thousandths of a unit off where the camera stood, where the camera's
        /// own is the double a `view` puts back exactly. Both look along `Camera::getOrient`.
        void take(const Rtx::AirClock& air);

        /// The weather and the hour the run stands under, `Thunderstorm, 14:32`, and which weather
        /// is crossing in where one is, `Clear → Overcast 37%, 14:32`, off the last note; empty until
        /// a stop has begun and been noted.
        std::string_view describeTitle();

        /// Where the run was left, as a stop that would put a camera back there, or null where no
        /// frame of a stop was noted.
        const Stop* getLeft() const
        {
            return mStood.has_value() && mStood->mStand.mEye.has_value() ? &*mStood : nullptr;
        }

    private:
        /// The note itself. The weather is assigned per frame into room the string already has.
        std::optional<Stop> mStood;

        /// What was crossing into that weather on the same frame, and how far. Beside the note and
        /// not in it, because a stop names the weather it stands under, and what is arriving is the
        /// title's alone. Empty while nothing is.
        std::string_view mArriving;
        float mCrossed = 0.0f;

        /// The cell the note names, by id: what a frame compares against to spell it only when the
        /// player crosses into another. An id and not the store, since a load frees every store and
        /// may build the next where the last one was.
        ESM::RefId mNotedCell;

        /// What `describeTitle` is written into, once a second and never allocated: room for the
        /// longest note `writeSkyNote` names.
        std::array<char, 48> mTitleNote{};
    };
}
