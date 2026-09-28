#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <components/rtx/common/monitor.hpp>
#include <components/rtx/common/worker.hpp>

namespace MWRender
{
    struct FrameReport;
}

namespace Rtx
{
    struct FrameExtents;
}

namespace RtxTool
{
    struct Stop;

    /// What Home does in a window: keeps the frame it went down on. The note of that frame is
    /// printed, the frame's picture is written with the note inside it as the PNG's `Description`,
    /// and the note is appended to a film's keys where the run names a file for them — so the note
    /// and the picture a person shares are of one frame, however long they looked around first.
    ///
    /// **The picture is the device's copy of the frame, a frame or two behind it.** Home asks for
    /// the copy before the frame is traced, and the note is kept until the copy comes back under
    /// that frame's number.
    ///
    /// **Written off the frame**, as the engine's own screenshot key writes its file: a PNG of a
    /// shown frame is tens of milliseconds, and a window somebody is looking through does not stall
    /// for a key. The frame copies the picture into a room this owns and a thread of its own writes
    /// it, in the order the presses came; the frame prints where each went, because the stream is
    /// the frame thread's. A room is used again once it is printed, so presses allocate only while
    /// they come faster than pictures are written.
    class HomeKey
    {
    public:
        /// `keys` is where a film's key is appended, or empty for none; `pictures` is the directory
        /// the pictures go into.
        HomeKey(std::filesystem::path keys, std::filesystem::path pictures);

        /// Waits for a picture already handed over to be written and prints it, so closing a window
        /// straight after a press still keeps it.
        ~HomeKey();

        /// Before a frame is traced: Home going down is a `press`.
        void listen();

        /// Asks for the picture of the next frame that can be copied (`wantsPicture`).
        void press() { mAsked = true; }

        /// Whether the frame about to be traced is one Home asked for, so the device copies it.
        /// **Not while the last press's copy is on its way**: a press then waits for it and takes
        /// the frame after, where it would otherwise take the note from under the first press.
        bool wantsPicture() const { return mAsked && !mFrame.has_value(); }

        /// After the frame `report` is of, with `left` its note (`StandingNote::getLeft`): prints the
        /// pictures written since the last frame, keeps the note where Home asked for this frame,
        /// and hands the picture over, at `extents`, where the device's copy of that frame is in
        /// `report`.
        void answer(const Stop* left, const MWRender::FrameReport& report, const Rtx::FrameExtents& extents);

    private:
        /// Who may touch a room: the frame while it is `Empty` or `Developed`, the writer while it is
        /// `Developing`, and neither while it is `Waiting`. It changes under `mMonitor`, which is
        /// what hands the room's contents from one thread to the other.
        enum class RoomState
        {
            Empty,
            Waiting,
            Developing,
            Developed,
        };

        /// One picture on its way to a file: the pixels, what they are of, under which id they are
        /// named, and which press it was, so the writer takes them in order; then what the frame is
        /// to print for it.
        struct Darkroom
        {
            std::vector<std::uint8_t> mPixels;
            std::uint32_t mWidth = 0;
            std::uint32_t mHeight = 0;
            std::string mNote;
            std::string mId;
            std::uint64_t mPress = 0;
            std::string mSaid;
            RoomState mState = RoomState::Empty;
        };

        /// Under `mMonitor`: whether any room is in `state`, and the one of them pressed first.
        bool anyIn(RoomState state) const;
        Darkroom* firstIn(RoomState state);

        /// The writer's turn on `room`: writes the picture at the next free number, and says where
        /// it went, or why it was not written, into `Darkroom::mSaid`.
        void develop(Darkroom& room);

        /// The frame's side: prints every developed room, in the order of the presses, and empties it.
        void collect();

        const std::filesystem::path mKeys;
        const std::filesystem::path mPictures;

        bool mHeld = false;
        bool mAsked = false;

        /// The note of the frame Home asked for, whole, and the backend's number of that frame,
        /// while the device's copy is on its way. A string a press, never one a frame.
        std::string mNote;
        std::string mId;
        std::optional<std::uint64_t> mFrame;

        /// A deque, because a room the writer is developing outside the lock must keep its address
        /// while the frame adds another.
        std::deque<Darkroom> mRooms;
        std::uint64_t mPresses = 0;
        /// The room the writer picked up, from its `take` to the end of its turn: the writer's alone.
        Darkroom* mDeveloping = nullptr;
        Rtx::Monitor mMonitor;

        /// Last, for the reason `Rtx::Worker` gives.
        Rtx::Worker mWriter;
    };
}
