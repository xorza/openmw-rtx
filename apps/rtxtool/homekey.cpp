#include "homekey.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <fstream>
#include <ios>
#include <ostream>
#include <utility>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/renderer/png.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "model/benchrun.hpp"
#include "run.hpp"

namespace RtxTool
{
    HomeKey::HomeKey(std::filesystem::path keys, std::filesystem::path pictures)
        : mKeys(std::move(keys))
        , mPictures(std::move(pictures))
    {
    }

    HomeKey::~HomeKey()
    {
        if (mWriter.isRunning())
            mMonitor.await([this] { return !anyIn(RoomState::Waiting) && !anyIn(RoomState::Developing); });
        collect();
    }

    void HomeKey::listen()
    {
        // **SDL's own key state, and not a script.** The other keys a window answers are named in
        // `keys.lua`, because what they do is turn the world, which only a script may; what this
        // one does is keep the session's own note of the frame, which no script can reach. The
        // state array is the engine's, pumped once a frame on this thread before the host's turn.
        const bool down = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_HOME];
        if (down && !mHeld)
            press();
        mHeld = down;
    }

    void HomeKey::answer(const Stop* left, const MWRender::FrameReport& report, const Rtx::FrameExtents& extents)
    {
        collect();

        if (wantsPicture() && left != nullptr)
        {
            mAsked = false;
            mNote = describeStanding(*left);
            mId = describeId(*left);
            mFrame = report.mFrame;

            if (!mKeys.empty())
            {
                std::ofstream keys(mKeys, std::ios::app);
                keys << '\n' << describeKey(*left);
                keys.flush();
                if (keys)
                    Log(Debug::Info) << "Ray tracing session: a film's key appended to "
                                     << Files::pathToUnicodeString(mKeys);
                else
                    Log(Debug::Error) << "Ray tracing session: could not append a film's key to "
                                      << Files::pathToUnicodeString(mKeys);
            }
        }

        // A result answers for one frame, a frame or two behind, and in order: one past the asked
        // frame means its copy will not come.
        if (!mFrame.has_value() || !report.mResult.has_value() || report.mResult->mFrame < *mFrame)
            return;

        const Rtx::FrameResult& finished = *report.mResult;
        const bool found = finished.mFrame == *mFrame && !finished.mPixels.empty();
        mFrame.reset();

        if (!found)
        {
            Debug::getRawStdout() << mNote << "# picture not taken: the device did not copy this frame\n" << std::flush;
            return;
        }

        // **Copied, because the renderer's copy is gone at its next frame.** Into a room an earlier
        // press grew wherever one is free, so a press allocates nothing but its note once there are
        // as many rooms as presses come in the time one picture takes to write.
        mMonitor.give([&] {
            auto room = std::find_if(
                mRooms.begin(), mRooms.end(), [](const Darkroom& one) { return one.mState == RoomState::Empty; });
            Darkroom& filled = room != mRooms.end() ? *room : mRooms.emplace_back();

            filled.mPixels.assign(finished.mPixels.begin(), finished.mPixels.end());
            filled.mWidth = extents.mOutputWidth;
            filled.mHeight = extents.mOutputHeight;
            filled.mNote = mNote;
            filled.mId = mId;
            filled.mPress = mPresses++;
            filled.mState = RoomState::Waiting;
        });

        if (mWriter.isRunning())
            return;

        mWriter.start("home key", [this](const Platform::StopToken& stop) {
            mMonitor.serve(
                stop, [this] { return anyIn(RoomState::Waiting); },
                [this] {
                    mDeveloping = firstIn(RoomState::Waiting);
                    Crash::contract(mDeveloping != nullptr, "the Home writer picked up a picture with none waiting");
                    mDeveloping->mState = RoomState::Developing;
                },
                [this](const Platform::StopToken&) {
                    develop(*mDeveloping);
                    mMonitor.hand([this] { mDeveloping->mState = RoomState::Developed; });
                });
        });
    }

    bool HomeKey::anyIn(const RoomState state) const
    {
        return std::any_of(
            mRooms.begin(), mRooms.end(), [state](const Darkroom& room) { return room.mState == state; });
    }

    HomeKey::Darkroom* HomeKey::firstIn(const RoomState state)
    {
        Darkroom* first = nullptr;
        for (Darkroom& room : mRooms)
            if (room.mState == state && (first == nullptr || room.mPress < first->mPress))
                first = &room;
        return first;
    }

    void HomeKey::collect()
    {
        mMonitor.under([this] {
            while (Darkroom* const room = firstIn(RoomState::Developed))
            {
                Debug::getRawStdout() << room->mNote << room->mSaid << std::flush;
                room->mState = RoomState::Empty;
            }
        });
    }

    void HomeKey::develop(Darkroom& room)
    {

        // Written or said why not: a directory nobody can write to is no reason to close a window
        // somebody is looking through.
        try
        {
            // **The next free number and never an overwrite**, so a second window writing into the
            // same directory keeps the first one's pictures.
            std::filesystem::create_directories(mPictures);
            std::filesystem::path path;
            for (std::uint32_t number = 1; path.empty() || std::filesystem::exists(path); ++number)
                path = mPictures / std::format("{}-{}.png", room.mId, number);

            Rtx::writePng(path, room.mWidth, room.mHeight, room.mPixels, room.mNote);
            room.mSaid = std::format("# picture {}\n", Files::pathToUnicodeString(path));
        }
        catch (const std::exception& refused)
        {
            room.mSaid = std::format("# picture not written: {}\n", refused.what());
        }
    }
}
