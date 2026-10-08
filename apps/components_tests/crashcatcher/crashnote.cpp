#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <latch>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/platform/process.hpp>

namespace
{
    /// The table as the monitor reads it, with this thread first: every test reads it while the
    /// threads that wrote it wait, as the monitor reads a process that stands still.
    Crash::NotesRead readAll()
    {
        Crash::NotesRead read;
        Crash::readNotes(Crash::noteTable(), Platform::Process::currentThreadId(), 0, false, read);
        return read;
    }

    const Crash::NoteCopy* findThread(const Crash::NotesRead& read, std::uint64_t thread)
    {
        const auto end = read.mNotes + read.mCount;
        const auto found
            = std::find_if(read.mNotes, end, [&](const Crash::NoteCopy& one) { return one.mThread == thread; });
        return found == end ? nullptr : found;
    }

    /// A note reads back as it was written, subject in quotes, whole, and first where its thread is
    /// the one named. A note with no subject is its words alone.
    TEST(CrashNoteTest, aNoteReadsBackAsWrittenAndItsThreadsComesFirst)
    {
        {
            const Crash::NoteScope noted("describing the texture \"{}\"", "textures/tx_a_rock.dds");
            const Crash::NotesRead read = readAll();
            ASSERT_GT(read.mCount, 0u);
            EXPECT_EQ(std::string_view(read.mNotes[0].mText), "describing the texture \"textures/tx_a_rock.dds\"");
            EXPECT_EQ(read.mNotes[0].mThread, Platform::Process::currentThreadId());
            EXPECT_TRUE(read.mNotes[0].mWhole);
        }

        const Crash::NoteScope noted("loading");
        const Crash::NotesRead read = readAll();
        ASSERT_GT(read.mCount, 0u);
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "loading");
    }

    /// Two threads' notes stand side by side, each under the system's id of the thread that wrote
    /// it.
    TEST(CrashNoteTest, eachThreadKeepsItsOwnNote)
    {
        const Crash::NoteScope uploading("uploading");

        std::latch noted(1);
        std::latch read(1);
        std::uint64_t other = 0;
        std::thread worker([&] {
            other = Platform::Process::currentThreadId();
            const Crash::NoteScope staging("staging the texture \"{}\"", "textures/tx_b.dds");
            noted.count_down();
            read.wait();
        });

        noted.wait();
        const Crash::NotesRead both = readAll();
        const Crash::NoteCopy* const mine = findThread(both, Platform::Process::currentThreadId());
        const Crash::NoteCopy* const theirs = findThread(both, other);
        ASSERT_NE(mine, nullptr);
        ASSERT_NE(theirs, nullptr);
        EXPECT_NE(other, Platform::Process::currentThreadId());
        EXPECT_EQ(std::string_view(mine->mText), "uploading");
        EXPECT_EQ(std::string_view(theirs->mText), "staging the texture \"textures/tx_b.dds\"");

        read.count_down();
        worker.join();
    }

    /// The table holds one note a thread for as many threads as it has slots, and a thread past
    /// them notes nothing rather than over another's. This thread holds one slot, so of as many
    /// threads again as there are slots, all but one hold the rest. As they end, the slots come
    /// back: a second crowd fills the table again, which it could not where an ended thread kept
    /// its slot.
    TEST(CrashNoteTest, aThreadPastTheTableNotesNothingAndEndedThreadsGiveTheirSlotsBack)
    {
        const Crash::NoteScope noted("filling the table");

        for (int crowd = 0; crowd < 2; ++crowd)
        {
            std::latch noting(Crash::sNoteThreads);
            std::latch read(1);
            std::vector<std::thread> threads;
            for (std::size_t i = 0; i < Crash::sNoteThreads; ++i)
                threads.emplace_back([&, i] {
                    const Crash::NoteScope mine("thread \"{}\"", std::to_string(i));
                    noting.count_down();
                    read.wait();
                });

            noting.wait();
            const Crash::NotesRead full = readAll();
            EXPECT_EQ(full.mCount, Crash::sNoteThreads) << "crowd " << crowd;
            EXPECT_EQ(std::string_view(full.mNotes[0].mText), "filling the table");

            read.count_down();
            for (std::thread& thread : threads)
                thread.join();
        }
    }

    /// A note longer than its slot is cut to it: one word, the two characters opening the quote
    /// and 252 of the subject's 300 make 255, the slot's 256 less its terminator, and the closing
    /// quote is what goes. A scope over a note of 200 is cut the same way — the 200, " > ", "b"
    /// and " \"" make 206, and 49 of the subject fill it to 255 — and its end puts back the 200
    /// exactly.
    TEST(CrashNoteTest, aNoteLongerThanItsSlotIsCutToIt)
    {
        {
            const Crash::NoteScope noted("a \"{}\"", std::string(300, 'x'));
            const Crash::NotesRead read = readAll();
            ASSERT_GT(read.mCount, 0u);
            EXPECT_EQ(std::string_view(read.mNotes[0].mText), "a \"" + std::string(252, 'x'));
            EXPECT_EQ(Crash::sNoteCapacity, 256u);
        }

        const std::string found(200, 'a');
        const Crash::NoteScope noted(found);
        {
            const Crash::NoteScope deep("b \"{}\"", std::string(100, 'c'));
            EXPECT_EQ(std::string_view(readAll().mNotes[0].mText), found + " > b \"" + std::string(49, 'c'));
        }
        EXPECT_EQ(std::string_view(readAll().mNotes[0].mText), found);
    }

    /// **A scope adds to the note and puts back what it found**: nested scopes read as the path
    /// through them, and a scope over an empty note begins it with no separator. A thread whose
    /// note is empty is doing nothing it noted, and the monitor reads no line for it. A
    /// formatted scope is its format's text.
    TEST(CrashNoteTest, aScopeAddsToTheNoteAndPutsBackWhatItFound)
    {
        const auto mine = [] {
            const Crash::NotesRead read = readAll();
            const Crash::NoteCopy* const note = findThread(read, Platform::Process::currentThreadId());
            return note == nullptr ? std::string("(no line)") : std::string(note->mText);
        };

        EXPECT_EQ(mine(), "(no line)");
        {
            const Crash::NoteScope frame("drawing frame 12");
            EXPECT_EQ(mine(), "drawing frame 12");
            {
                const Crash::NoteScope scene("building the scene");
                EXPECT_EQ(mine(), "drawing frame 12 > building the scene");
                {
                    const Crash::NoteScope texture("staging the texture \"{}\"", "textures/tx_a.dds");
                    EXPECT_EQ(
                        mine(), "drawing frame 12 > building the scene > staging the texture \"textures/tx_a.dds\"");
                }
                EXPECT_EQ(mine(), "drawing frame 12 > building the scene");
            }
            EXPECT_EQ(mine(), "drawing frame 12");
        }
        EXPECT_EQ(mine(), "(no line)");

        {
            const Crash::NoteScope formatted("drawing frame {} at {}x{}", 812345, 3840, 2160);
            EXPECT_EQ(mine(), "drawing frame 812345 at 3840x2160");
        }
    }

    /// **One report at a time, and the one before it back afterwards.** A request that finds a
    /// report in progress changes nothing — the hang request that landed inside a report's window
    /// wrote over its kind and its reason — and a report that ends puts back what it found, so
    /// the next crash reads as a crash with the reason it had, and not as the report before it.
    TEST(CrashNoteTest, aReportInProgressRefusesAnotherAndPutsBackWhatItFound)
    {
        const auto kindAndReason = [] {
            Crash::NotesRead read;
            Crash::readNotes(Crash::noteTable(), 0, 0, false, read);
            return std::pair{ read.mKind, std::string(read.mReason) };
        };

        const auto before = kindAndReason();
        EXPECT_FALSE(Crash::isReporting());

        ASSERT_TRUE(Crash::beginReport(Crash::ReportKind::Report, "asked"));
        EXPECT_TRUE(Crash::isReporting());
        EXPECT_EQ(kindAndReason(), std::pair(Crash::ReportKind::Report, std::string("asked")));

        EXPECT_FALSE(Crash::beginReport(Crash::ReportKind::Hang, {})) << "a hang request inside a report";
        EXPECT_EQ(kindAndReason(), std::pair(Crash::ReportKind::Report, std::string("asked")))
            << "the refused request wrote over the report in progress";

        Crash::endReport();
        EXPECT_FALSE(Crash::isReporting());
        EXPECT_EQ(kindAndReason(), before) << "what the report found was not put back";

        ASSERT_TRUE(Crash::beginReport(Crash::ReportKind::Hang, {})) << "the gate did not open again";
        Crash::endReport();
    }

    /// **The table as the monitor reads it**: a copy of its bytes, taken from outside, put in the
    /// order a report wants, with the thread it is about first and what the next report is and why.
    /// A hang is about the thread that draws, not the one that took the request. A note whose thread
    /// stopped in the middle of writing it reads as half written, and a copy of another size is no
    /// table, and reads as nothing noted.
    TEST(CrashNoteTest, aCopyOfTheTableReadsAsTheLiveOneWithTheNamedThreadFirst)
    {
        const Crash::NoteScope drawing("drawing");
        std::uint64_t other = 0;
        std::latch noted(1);
        std::latch copied(1);
        std::thread worker([&] {
            other = Platform::Process::currentThreadId();
            const Crash::NoteScope walking("walking the cell \"{}\"", "Seyda Neen");
            noted.count_down();
            copied.wait();
        });
        noted.wait();

        ASSERT_TRUE(Crash::beginReport(Crash::ReportKind::Report, "a contract broken"));
        const std::span<const std::byte> live = Crash::noteTable();
        const std::vector<std::byte> copy(live.begin(), live.end());
        Crash::endReport();
        ASSERT_TRUE(Crash::beginReport(Crash::ReportKind::Hang, {}));
        const std::vector<std::byte> hanging(live.begin(), live.end());
        Crash::endReport();
        copied.count_down();
        worker.join();

        Crash::NotesRead read;
        Crash::readNotes(copy, other, 0, false, read);
        EXPECT_EQ(read.mKind, Crash::ReportKind::Report);
        EXPECT_EQ(std::string_view(read.mReason), "a contract broken");
        ASSERT_EQ(read.mCount, 2u);
        EXPECT_EQ(read.mNotes[0].mThread, other);
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "walking the cell \"Seyda Neen\"");
        EXPECT_EQ(std::string_view(read.mNotes[1].mText), "drawing");
        EXPECT_TRUE(read.mNotes[0].mWhole && read.mNotes[1].mWhole);
        EXPECT_EQ(read.mThread, other);

        // The worker took the hang request here, and this thread draws: the hang is this thread's,
        // and with no thread known to draw, the one that asked stands for it. A fault that lands
        // under the hang is the faulting thread's.
        const std::uint64_t self = Platform::Process::currentThreadId();
        Crash::readNotes(hanging, other, self, false, read);
        EXPECT_EQ(read.mKind, Crash::ReportKind::Hang);
        EXPECT_EQ(read.mThread, self) << "a hang named the thread that took the request";
        ASSERT_EQ(read.mCount, 2u);
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "drawing");
        EXPECT_EQ(std::string_view(read.mNotes[1].mText), "walking the cell \"Seyda Neen\"");
        Crash::readNotes(hanging, other, 0, false, read);
        EXPECT_EQ(read.mThread, other);
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "walking the cell \"Seyda Neen\"");
        Crash::readNotes(hanging, other, self, true, read);
        EXPECT_EQ(read.mKind, Crash::ReportKind::Crash);
        EXPECT_EQ(read.mThread, other) << "a fault under a hang named the thread that draws";
        // A report the game asked for is the asking thread's whoever draws.
        Crash::readNotes(copy, other, self, false, read);
        EXPECT_EQ(read.mThread, other);

        // **A fault taken while the report was written is a crash**, with no reason: the table's
        // kind and reason are the report's, which the faulting thread never passed the gate for.
        // Its notes are its own all the same.
        Crash::readNotes(copy, other, 0, true, read);
        EXPECT_EQ(read.mKind, Crash::ReportKind::Crash) << "a fault read as the report it landed in";
        EXPECT_EQ(std::string_view(read.mReason), "") << "a fault was given the report's reason";
        EXPECT_EQ(read.mCount, 2u);

        // A table that says crash keeps its reason under a fault: `std::terminate`'s abort raises
        // a signal after saying why.
        ASSERT_TRUE(Crash::beginReport(Crash::ReportKind::Crash, "std::terminate"));
        const std::vector<std::byte> ending(Crash::noteTable().begin(), Crash::noteTable().end());
        Crash::endReport();
        Crash::readNotes(ending, other, 0, true, read);
        EXPECT_EQ(read.mKind, Crash::ReportKind::Crash);
        EXPECT_EQ(std::string_view(read.mReason), "std::terminate");

        // A slot is its thread's id and then its sequence count, which a note in progress leaves
        // odd: one step on from the count the copy holds is the worker stopped halfway.
        std::vector<std::byte> halfway = copy;
        std::vector<std::size_t> slots;
        for (std::size_t at = 0; at + sizeof(other) <= halfway.size(); at += alignof(std::uint64_t))
            if (std::memcmp(halfway.data() + at, &other, sizeof(other)) == 0)
                slots.push_back(at);
        ASSERT_EQ(slots.size(), 1u) << "the worker's id is not in one place of the table";
        std::uint32_t sequence = 0;
        std::memcpy(&sequence, halfway.data() + slots[0] + sizeof(other), sizeof(sequence));
        ++sequence;
        std::memcpy(halfway.data() + slots[0] + sizeof(other), &sequence, sizeof(sequence));

        Crash::readNotes(halfway, other, 0, false, read);
        ASSERT_EQ(read.mCount, 2u);
        EXPECT_FALSE(read.mNotes[0].mWhole) << "a note stopped halfway read as whole";
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "walking the cell \"Seyda Neen\"");
        EXPECT_TRUE(read.mNotes[1].mWhole);

        Crash::readNotes(std::span(copy).first(copy.size() - 1), other, 0, false, read);
        EXPECT_EQ(read.mCount, 0u) << "a table of another size was read";
        EXPECT_EQ(read.mKind, Crash::ReportKind::Crash);

        EXPECT_EQ(readAll().mKind, Crash::ReportKind::Crash) << "the report kind outlived its report";
    }
}
