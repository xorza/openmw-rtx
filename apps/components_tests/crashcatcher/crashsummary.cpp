#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/crashcatcher/crashsummary.hpp>

namespace
{
    Crash::NoteCopy noteOf(std::uint64_t thread, const char* text, bool whole = true)
    {
        Crash::NoteCopy note{};
        std::strncpy(note.mText, text, sizeof(note.mText) - 1);
        note.mThread = thread;
        note.mWhole = whole;
        return note;
    }

    /// A fault, word for word: the exception and the thread that raised it, where it stopped,
    /// what its stack points into, every thread's note with the crashed one marked and a half
    /// written one said to be, what the game said of itself, and the dump.
    TEST(CrashSummaryTest, aFaultIsSummarisedLineByLine)
    {
        Crash::CrashFacts facts;
        facts.mException = "EXCEPTION_ACCESS_VIOLATION reading 0x12204cfe000";
        facts.mNotes.mThread = 12632;
        facts.mWhere = "VCRUNTIME140.dll+0x1dd06";
        facts.mStack = { "openmw.exe+0x112a9a7", "openmw.exe+0x112b1f7" };
        facts.mNotes.mCount = 2;
        facts.mNotes.mNotes[0] = noteOf(12632, "staging the texture \"textures/tx_a.dds\"");
        facts.mNotes.mNotes[1] = noteOf(8812, "describing the texture \"textures/tx_b", false);
        facts.mAnnotations = { { "product", "OpenMW" }, { "renderer", "ray tracing" } };
        facts.mDump = "C:/Users/x/crashes/reports/1.dmp";

        std::vector<std::string> lines;
        Crash::summarise(facts, lines, Crash::SummaryReaders::Player);
        const std::vector<std::string> expected{
            "Crash: EXCEPTION_ACCESS_VIOLATION reading 0x12204cfe000 in thread 12632",
            "Crash: at VCRUNTIME140.dll+0x1dd06",
            "Crash: return addresses on its stack: openmw.exe+0x112a9a7 openmw.exe+0x112b1f7",
            "Crash: note of thread 12632, which crashed: staging the texture \"textures/tx_a.dds\"",
            "Crash: note of thread 8812: describing the texture \"textures/tx_b (half written)",
            "Crash: product: OpenMW",
            "Crash: renderer: ray tracing",
            "Crash: dump C:/Users/x/crashes/reports/1.dmp",
        };
        EXPECT_EQ(lines, expected);
        EXPECT_EQ(Crash::title(facts), "Crash: EXCEPTION_ACCESS_VIOLATION reading 0x12204cfe000");

        // **The public reads the dump by its file alone**: the folder is the user's, and names the
        // account. The rest is the player's own summary, line for line.
        std::vector<std::string> shared;
        Crash::summarise(facts, shared, Crash::SummaryReaders::Public);
        std::vector<std::string> sharedExpected = expected;
        sharedExpected.back() = "Crash: dump 1.dmp";
        EXPECT_EQ(shared, sharedExpected);
        for (const std::string& line : shared)
            EXPECT_EQ(line.find("Users/x"), std::string::npos) << line;

        // The words a system's facts are spelt in: an address in hexadecimal, nought included, and a
        // code by its name, or by what the system half says where it has none.
        EXPECT_EQ(Crash::hex(0x12204cfe000), "0x12204cfe000");
        EXPECT_EQ(Crash::hex(0), "0x0");
        constexpr std::array<std::pair<std::uint32_t, std::string_view>, 2> names{ {
            { 11, "SIGSEGV" },
            { 6, "SIGABRT" },
        } };
        EXPECT_EQ(Crash::nameOf(names, 6, "signal 6"), "SIGABRT");
        EXPECT_EQ(Crash::nameOf(names, 7, "signal 7"), "signal 7");
    }

    /// A reason given by the code that asked leads, and the exception it was raised as follows
    /// it: `std::terminate`'s abort names nothing of its cause. A report the game asked for marks
    /// the thread that asked, and a hang the thread that draws. With nothing known, each says so
    /// rather than leaving a line out.
    TEST(CrashSummaryTest, eachKindHeadsItsLinesAndMarksItsThread)
    {
        Crash::CrashFacts terminate;
        std::strcpy(terminate.mNotes.mReason, "std::terminate on an uncaught exception: bad");
        terminate.mException = "SIGABRT";
        terminate.mNotes.mThread = 7;
        std::vector<std::string> lines;
        Crash::summarise(terminate, lines, Crash::SummaryReaders::Player);
        EXPECT_EQ(lines[0], "Crash: std::terminate on an uncaught exception: bad in thread 7");
        EXPECT_EQ(Crash::title(terminate), "Crash: std::terminate on an uncaught exception: bad");
        EXPECT_EQ(lines[1], "Crash: raised as SIGABRT");
        EXPECT_EQ(lines[2], "Crash: no thread noted anything");
        EXPECT_EQ(lines[3], "Crash: no dump was written");
        EXPECT_EQ(lines.size(), 4u);

        Crash::CrashFacts report;
        report.mNotes.mKind = Crash::ReportKind::Report;
        std::strcpy(report.mNotes.mReason, "a contract broken");
        report.mNotes.mThread = 7;
        report.mNotes.mCount = 1;
        report.mNotes.mNotes[0] = noteOf(7, "placing");
        lines.clear();
        Crash::summarise(report, lines, Crash::SummaryReaders::Player);
        EXPECT_EQ(lines[0], "Report: a contract broken in thread 7");
        EXPECT_EQ(Crash::title(report), "Report: a contract broken");
        EXPECT_EQ(lines[1], "Report: note of thread 7, which asked: placing");

        Crash::CrashFacts hang;
        hang.mNotes.mKind = Crash::ReportKind::Hang;
        hang.mStalledFor = 20;
        hang.mNotes.mThread = 7;
        hang.mNotes.mCount = 2;
        hang.mNotes.mNotes[0] = noteOf(7, "compiling");
        hang.mNotes.mNotes[1] = noteOf(9, "walking");
        lines.clear();
        Crash::summarise(hang, lines, Crash::SummaryReaders::Player);
        EXPECT_EQ(lines[0], "Hang: no frame for 20 seconds in thread 7");
        EXPECT_EQ(Crash::title(hang), "Hang: no frame for 20 seconds");
        EXPECT_EQ(lines[1], "Hang: note of thread 7, which draws: compiling");
        EXPECT_EQ(lines[2], "Hang: note of thread 9: walking");

        Crash::CrashFacts nothing;
        lines.clear();
        Crash::summarise(nothing, lines, Crash::SummaryReaders::Player);
        EXPECT_EQ(lines[0], "Crash: no exception was recorded");
        EXPECT_EQ(Crash::title(nothing), "Crash: no exception was recorded");
    }

    /// What a terminate handler reports, for each thing that can be current: the message of a
    /// `std::exception`, a word for anything else thrown, and the bare call where nothing is.
    ///
    /// **A message longer than a note is cut to what a note holds**, 255 bytes of the 256: the text
    /// goes into the caller's buffer, because nothing between `std::terminate` and the abort may
    /// allocate.
    TEST(CrashSummaryTest, aTerminateNamesWhatWasThrown)
    {
        char reason[Crash::sNoteCapacity];
        try
        {
            throw std::runtime_error("a storage that cannot be read");
        }
        catch (...)
        {
            EXPECT_EQ(Crash::terminateReason(reason),
                "std::terminate on an uncaught exception: a storage that cannot be read");
        }

        try
        {
            throw 4;
        }
        catch (...)
        {
            EXPECT_EQ(
                Crash::terminateReason(reason), "std::terminate on an uncaught exception that is no std::exception");
        }

        try
        {
            throw std::runtime_error(std::string(400, 'x'));
        }
        catch (...)
        {
            const std::string_view cut = Crash::terminateReason(reason);
            EXPECT_EQ(cut.size(), Crash::sNoteCapacity - 1);
            EXPECT_EQ(
                cut, "std::terminate on an uncaught exception: " + std::string(Crash::sNoteCapacity - 1 - 41, 'x'));
        }

        EXPECT_EQ(Crash::terminateReason(reason), "std::terminate");
    }
}
