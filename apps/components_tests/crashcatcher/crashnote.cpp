#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/platform/process.hpp>

namespace
{
    /// The table as the monitor reads it, with this thread first: every test reads it while the
    /// threads that wrote it wait, as the monitor reads a process that stands still.
    Crash::NotesRead readAll()
    {
        Crash::NotesRead read;
        Crash::readNotes(Crash::noteTable(), Platform::Process::currentThreadId(), read);
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
    /// them notes nothing rather than over another's, **and is counted once**, however often it
    /// tries. This thread holds one slot, so of as many threads again as there are slots, all but
    /// one hold the rest. As they end, the slots come back: a second crowd fills the table again,
    /// which it could not where an ended thread kept its slot.
    TEST(CrashNoteTest, aThreadPastTheTableNotesNothingAndEndedThreadsGiveTheirSlotsBack)
    {
        const Crash::NoteScope noted("filling the table");

        for (int crowd = 0; crowd < 2; ++crowd)
        {
            const std::uint32_t refusedBefore = readAll().mRefused;
            std::latch noting(Crash::sNoteThreads);
            std::latch read(1);
            std::vector<std::thread> threads;
            for (std::size_t i = 0; i < Crash::sNoteThreads; ++i)
                threads.emplace_back([&, i] {
                    const Crash::NoteScope mine("thread \"{}\"", std::to_string(i));
                    const Crash::NoteScope again("again");
                    noting.count_down();
                    read.wait();
                });

            noting.wait();
            const Crash::NotesRead full = readAll();
            EXPECT_EQ(full.mCount, Crash::sNoteThreads) << "crowd " << crowd;
            EXPECT_EQ(full.mRefused, refusedBefore + 1) << "crowd " << crowd;
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

    /// **A cut falls before a character and never inside one**, whichever of the two cuts it: the
    /// format's 255 bytes leave "ab \"" and 251 of `Ω`'s two-byte spelling, 125 characters and half
    /// of one, which goes; and a scope over a note of 201 leaves 51 bytes after " > ", 25
    /// characters and half of one, which goes too.
    TEST(CrashNoteTest, aCutFallsBeforeACharacterAndNeverInsideOne)
    {
        std::string omega;
        for (int i = 0; i < 200; ++i)
            omega += reinterpret_cast<const char*>(u8"Ω");

        {
            const Crash::NoteScope noted("ab \"{}\"", omega);
            std::string expected = "ab \"";
            expected += omega.substr(0, 250);
            EXPECT_EQ(std::string_view(readAll().mNotes[0].mText), expected);
        }

        const std::string found(201, 'a');
        const Crash::NoteScope noted(found);
        const Crash::NoteScope deep(omega);
        EXPECT_EQ(std::string_view(readAll().mNotes[0].mText), found + " > " + omega.substr(0, 50));
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

    /// **The table as the monitor reads it**: a copy of its bytes, taken from outside, put in the
    /// order a report wants, with the thread it is about first, and the reason the process ends on. A
    /// note whose thread stopped in the middle of writing it reads as half written, and a copy of
    /// another size is no table, and reads as nothing noted.
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

        const std::span<const std::byte> live = Crash::noteTable();
        const std::vector<std::byte> copy(live.begin(), live.end());
        copied.count_down();
        worker.join();

        Crash::NotesRead read;
        Crash::readNotes(copy, other, read);
        EXPECT_EQ(std::string_view(read.mReason), "") << "no process ended, and no reason was given";
        ASSERT_EQ(read.mCount, 2u);
        EXPECT_EQ(read.mNotes[0].mThread, other);
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "walking the cell \"Seyda Neen\"");
        EXPECT_EQ(std::string_view(read.mNotes[1].mText), "drawing");
        EXPECT_TRUE(read.mNotes[0].mWhole);
        EXPECT_TRUE(read.mNotes[1].mWhole);
        EXPECT_EQ(read.mThread, other);

        // This thread first where it is the one named, as the one that draws is for a hang.
        const std::uint64_t self = Platform::Process::currentThreadId();
        Crash::readNotes(copy, self, read);
        EXPECT_EQ(read.mThread, self);
        ASSERT_EQ(read.mCount, 2u);
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "drawing");
        EXPECT_EQ(std::string_view(read.mNotes[1].mText), "walking the cell \"Seyda Neen\"");

        // With no thread named, the table's order.
        Crash::readNotes(copy, 0, read);
        EXPECT_EQ(read.mThread, 0u);
        EXPECT_EQ(read.mCount, 2u);

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

        Crash::readNotes(halfway, other, read);
        ASSERT_EQ(read.mCount, 2u);
        EXPECT_FALSE(read.mNotes[0].mWhole) << "a note stopped halfway read as whole";
        EXPECT_EQ(std::string_view(read.mNotes[0].mText), "walking the cell \"Seyda Neen\"");
        EXPECT_TRUE(read.mNotes[1].mWhole);

        Crash::readNotes(std::span(copy).first(copy.size() - 1), other, read);
        EXPECT_EQ(read.mCount, 0u) << "a table of another size was read";

        // The table travels as one Crashpad annotation, whose value is held below 20 KiB.
        EXPECT_LT(copy.size(), 5u * 4096u);
    }

    /// **The first reason the process ends on stands**: a second thread failing at the same time
    /// changes nothing. Its own process, since the reason is the table's for good.
    TEST(CrashNoteTest, theFirstEndingReasonStands)
    {
        EXPECT_EXIT(
            {
                Crash::endingReason("the first");
                Crash::endingReason("the second");
                Crash::NotesRead read;
                Crash::readNotes(Crash::noteTable(), 0, read);
                std::_Exit(std::string_view(read.mReason) == "the first" ? 7 : 8);
            },
            ::testing::ExitedWithCode(7), "");
    }

    /// **A scope ends on the thread it began on**: one ended on another would write that thread's
    /// note, and a debug build stops at it.
    TEST(CrashNoteTest, aScopeEndedOnAnotherThreadDies)
    {
        Rtx::Testing::expectAssertDies(
            [] {
                auto scope = std::make_unique<Crash::NoteScope>("moved across");
                std::thread([&] { scope.reset(); }).join();
            },
            "a note scope ended on a thread other than the one it began on");
    }

    /// **Crashpad's annotations lock nothing**, so they are set from one thread, the first to set one:
    /// a second dies in a debug build. In a process of its own, as the first thread is the process's.
    TEST(CrashNoteTest, annotationsFromASecondThreadDie)
    {
        Rtx::Testing::expectAssertDies(
            [] {
                Crash::annotate("first", "this thread");
                std::thread([] { Crash::annotate("second", "another thread"); }).join();
            },
            "Crash::annotate was called from a second thread");
    }
}
