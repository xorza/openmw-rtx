#include "crashnote.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>

#include <components/platform/process.hpp>

namespace Crash
{
    namespace
    {
        using Thread = std::atomic_ref<std::uint64_t>;
        using Word = std::atomic_ref<std::uint32_t>;
    }

    /// One thread's note, written by that thread alone and read by any.
    ///
    /// **A sequence counter and not a lock**, because the reader is the monitor, and a crash stops
    /// the owner anywhere: odd while the owner writes, so a note stopped halfway is known as one.
    /// Plain words reached through `std::atomic_ref`, so the table is trivially copyable and the
    /// monitor can read it as the bytes it took out of the game.
    struct NoteSlot
    {
        alignas(Thread::required_alignment) std::uint64_t mThread;
        alignas(Word::required_alignment) std::uint32_t mSequence;
        char mText[sNoteCapacity];
    };

    namespace
    {
        struct Table
        {
            char mReason[sNoteCapacity];
            alignas(Word::required_alignment) std::uint32_t mRefused;
            NoteSlot mSlots[sNoteThreads];
        };

        static_assert(std::is_trivially_copyable_v<Table>);

        // The layout `omw crash` reads the table's annotation by (`tools/omw/crash.py`).
        static_assert(offsetof(Table, mRefused) == sNoteCapacity);
        static_assert(offsetof(Table, mSlots) == 264);
        static_assert(sizeof(NoteSlot) == 272);
        static_assert(offsetof(NoteSlot, mSequence) == 8);
        static_assert(offsetof(NoteSlot, mText) == 12);

        // Crashpad's `Annotation::kValueMaxSize`, which the table travels as one value of.
        static_assert(sizeof(Table) < 5 * 4096);

        // A signal handler may touch only what never locks.
        static_assert(Thread::is_always_lock_free);
        static_assert(Word::is_always_lock_free);

        Table sTable{};

        /// Whether the process is ending on a report, whose reason the table then holds for good.
        std::atomic<bool> sEnding{ false };
        static_assert(std::atomic<bool>::is_always_lock_free);

        /// A thread's hold on its slot, given back as the thread ends, so that threads made and
        /// ended by the hundred do not fill the table with the dead.
        struct Claim
        {
            NoteSlot* mSlot = nullptr;

            /// Whether this thread found the table full and was counted for it.
            bool mRefused = false;

            Claim() = default;
            Claim(const Claim&) = delete;
            Claim& operator=(const Claim&) = delete;
            Claim(Claim&&) = delete;
            Claim& operator=(Claim&&) = delete;

            ~Claim()
            {
                if (mSlot != nullptr)
                    Thread(mSlot->mThread).store(0, std::memory_order_release);
            }
        };

        NoteSlot* claimSlot(std::uint64_t thread)
        {
            for (NoteSlot& slot : sTable.mSlots)
            {
                std::uint64_t free = 0;
                if (Thread(slot.mThread).compare_exchange_strong(free, thread, std::memory_order_acq_rel))
                    return &slot;
            }

            return nullptr;
        }

        /// How much of `text` is whole UTF-8 characters: all of it, but for a last character its end
        /// cut short, as a cut to a note's room or `std::format_to_n`'s leaves one.
        std::size_t wholeCharacters(std::string_view text)
        {
            for (std::size_t back = 1; back <= std::min<std::size_t>(4, text.size()); ++back)
            {
                const auto byte = static_cast<unsigned char>(text[text.size() - back]);
                if ((byte & 0xC0) == 0x80)
                    continue;
                const std::size_t needs = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
                return needs > back ? text.size() - back : text.size();
            }
            return text.size();
        }

        std::size_t append(char (&into)[sNoteCapacity], std::size_t at, std::string_view text)
        {
            // **Cut before a character and not inside one**: the summary that quotes a note goes
            // into the log and into an address, where half a UTF-8 character reads as none.
            const std::size_t length = wholeCharacters(text.substr(0, std::min(text.size(), sNoteCapacity - 1 - at)));
            std::copy_n(text.data(), length, into + at);
            return at + length;
        }

        /// The calling thread's hold on its slot, which it may not have claimed yet.
        Claim& ownClaim()
        {
            thread_local Claim claim;
            return claim;
        }

        /// The calling thread's slot, claimed on its first note and not before, so a thread that
        /// never notes holds none, and asked again while the table is full, so a thread that met it
        /// full notes once one ends. Null while the table is full.
        NoteSlot* ownSlot()
        {
            Claim& claim = ownClaim();
            if (claim.mSlot == nullptr)
            {
                claim.mSlot = claimSlot(Platform::Process::currentThreadId());
                if (claim.mSlot == nullptr && !claim.mRefused)
                {
                    claim.mRefused = true;
                    Word(sTable.mRefused).fetch_add(1, std::memory_order_relaxed);
                }
            }
            return claim.mSlot;
        }

        std::size_t lengthOf(const NoteSlot& slot)
        {
            return static_cast<std::size_t>(std::find(slot.mText, slot.mText + sNoteCapacity - 1, '\0') - slot.mText);
        }

        /// Runs `write` over the slot's text between the two steps of its sequence, and ends the
        /// text where `write` says.
        template <class Write>
        void rewrite(NoteSlot& slot, Write write)
        {
            Word(slot.mSequence).fetch_add(1, std::memory_order_acq_rel);
            slot.mText[write(slot.mText)] = '\0';
            Word(slot.mSequence).fetch_add(1, std::memory_order_release);
        }
    }

    NoteScope::NoteScope(std::string_view text)
    {
        begin(text);
    }

    void NoteScope::begin(std::string_view text)
    {
        mSlot = ownSlot();
        if (mSlot == nullptr)
            return;

        mFoundLength = lengthOf(*mSlot);
        std::memcpy(mFound, mSlot->mText, mFoundLength);
        rewrite(*mSlot, [&](char (&into)[sNoteCapacity]) {
            const std::size_t at = mFoundLength == 0 ? 0 : append(into, mFoundLength, " > ");
            return append(into, at, text);
        });
    }

    NoteScope::~NoteScope()
    {
        if (mSlot == nullptr)
            return;

        assert(Thread(mSlot->mThread).load(std::memory_order_relaxed) == Platform::Process::currentThreadId()
            && "a note scope ended on a thread other than the one it began on");
        rewrite(*mSlot, [&](char (&text)[sNoteCapacity]) {
            std::memcpy(text, mFound, mFoundLength);
            return mFoundLength;
        });
    }

    std::string_view ownNote()
    {
        // Read without a claim: a thread that never noted has nothing to say, and asking takes no slot.
        const NoteSlot* const slot = ownClaim().mSlot;
        if (slot == nullptr)
            return {};

        return std::string_view(slot->mText, lengthOf(*slot));
    }

    void endingReason(std::string_view reason)
    {
        if (sEnding.exchange(true, std::memory_order_acq_rel))
            return;

        sTable.mReason[append(sTable.mReason, 0, reason)] = '\0';
    }

    std::span<const std::byte> noteTable()
    {
        return std::as_bytes(std::span(&sTable, 1));
    }

    void readNotes(std::span<const std::byte> table, const std::uint64_t thread, NotesRead& into)
    {
        // Bytes out of another process, which a crash may have left any shape: a table of another
        // size is no table.
        into = NotesRead{};
        into.mThread = thread;
        if (table.size() != sizeof(Table))
            return;

        Table copy;
        std::memcpy(&copy, table.data(), sizeof(Table));
        std::memcpy(into.mReason, copy.mReason, sNoteCapacity);
        into.mReason[sNoteCapacity - 1] = '\0';
        into.mRefused = copy.mRefused;

        // The process stands still while its table is copied, so a note whose count is odd is one
        // its thread stopped in the middle of. Such a note is kept even where it reads empty:
        // something was being noted.
        const auto take = [&](const NoteSlot& slot) {
            const bool whole = slot.mSequence % 2 == 0;
            if (whole && slot.mText[0] == '\0')
                return;

            NoteCopy& note = into.mNotes[into.mCount++];
            note.mThread = slot.mThread;
            std::memcpy(note.mText, slot.mText, sNoteCapacity);
            note.mText[sNoteCapacity - 1] = '\0';
            note.mWhole = whole;
        };

        if (thread != 0)
            for (const NoteSlot& slot : copy.mSlots)
                if (slot.mThread == thread)
                    take(slot);

        for (const NoteSlot& slot : copy.mSlots)
            if (slot.mThread != 0 && slot.mThread != thread)
                take(slot);
    }
}
