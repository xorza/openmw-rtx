#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// **What the crash matrix knows of the system it runs on**: how each system names the crashes the
/// modes raise, and the modes only one system has, in `crashtestsposix.cpp` and
/// `crashtestswin32.cpp`.
namespace CrashTests
{
    /// What one mode must leave: the summary's first line holds `mHeadline` and one of `mRaised`
    /// where that is not empty, and a dump carries the same summary. A mode the game lives through
    /// leaves `mFollows` after it, and a mode that reports nothing leaves no line. A crash ends the
    /// game with a status other than nought, and every other mode with nought, but where `mEndsBy`
    /// names the signal.
    struct Mode
    {
        std::string_view mName;
        std::string_view mHeadline;
        std::vector<std::string_view> mRaised;
        std::string_view mFollows = {};
        bool mReports = true;

        /// How the note of the thread the report is about is marked, where that thread noted what it
        /// was doing: the main thread does, before any mode.
        std::string_view mMarked = {};

        /// Whether the dump must carry `sHeapMarker`, which lies on the heap and which only the
        /// crashing stack points at.
        bool mHeap = false;

        /// A report the mode leaves beside its own, by the start of its summary, of another kind and
        /// on another thread: a second summary and a second dump are then due, each its own.
        std::string_view mAlso = {};

        /// The signal the run must end by, or nought. A shell that runs the mode as its child says
        /// it as `128 + mEndsBy`.
        int mEndsBy = 0;

        /// Whether the mode runs under an AppImage's keeper (`Crash::keepImageMounted`), whose run
        /// ends only once the monitor has: the package then stands when the run ends, not after.
        bool mKept = false;
    };

    /// How this system names what the modes every system has raise, one of each list.
    struct Raised
    {
        std::vector<std::string_view> mFault;
        std::vector<std::string_view> mOverflow;
        std::vector<std::string_view> mIllegal;

        /// Whether Crashpad keeps what the crashing stack points at, which it scans for on Windows
        /// alone.
        bool mStackScanned = false;
    };

    Raised raisedOnThisSystem();

    /// Appends this system's own modes, and `abort`, which every system has and each names its own
    /// way. `crashed` marks the note of the thread that crashed.
    void addModesOfThisSystem(std::vector<Mode>& into, std::string_view crashed);

    /// What `mode` needs of the process before `wrapApplication` starts it, where it is one of this
    /// system's own.
    void prepareModeOfThisSystem(std::string_view mode);

    /// Runs `mode` where it is one of this system's own, and answers what the process exits with;
    /// nothing where it is none of them.
    std::optional<int> runModeOfThisSystem(std::string_view mode);

    /// What is wrong with what `mode`, one of this system's own, left in `folder` beyond what the
    /// shared check reads; nothing where all holds, or where it is none of them.
    std::optional<std::string> checkModeOfThisSystem(std::string_view mode, const std::filesystem::path& folder);

    /// An instruction the processor refuses.
    void illegalInstruction();

    /// What a mode the game lives through ends with: the line saying so, and nought. The shared
    /// file's.
    int livedOn();

    /// The whole of the file at `path`, or nothing where it cannot be read. The shared file's.
    std::string contentsOf(const std::filesystem::path& path);

    /// What a mode exits with where the run did not give it the case it tests, which a mode that
    /// races two threads cannot promise: the matrix empties its folder and runs it again, up to
    /// `sInconclusiveRuns` times. `EX_TEMPFAIL`'s number, which no mode's own end shares.
    constexpr int sInconclusive = 75;
    constexpr int sInconclusiveRuns = 8;
}
