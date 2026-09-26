#pragma once

#include <optional>
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
    /// game with a status other than nought, and every other mode with nought.
    struct Mode
    {
        std::string_view mName;
        std::string_view mHeadline;
        std::vector<std::string_view> mRaised;
        std::string_view mFollows = {};
        bool mReports = true;

        /// How the note of the thread that raised the report is marked, where that thread noted
        /// what it was doing: the main thread does, before any mode.
        std::string_view mMarked = {};

        /// Whether the dump must carry `sHeapMarker`, which lies on the heap and which only the
        /// crashing stack points at.
        bool mHeap = false;
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

    /// Runs `mode` where it is one of this system's own, and answers what the process exits with;
    /// nothing where it is none of them.
    std::optional<int> runModeOfThisSystem(std::string_view mode);

    /// An instruction the processor refuses.
    void illegalInstruction();

    /// What a mode the game lives through ends with: the line saying so, and nought. The shared
    /// file's.
    int livedOn();
}
