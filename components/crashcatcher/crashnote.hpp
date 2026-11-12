#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <utility>

namespace Crash
{
    /// How many bytes a note holds, its terminating nought included. A longer one is cut short.
    inline constexpr std::size_t sNoteCapacity = 256;

    /// **How many threads can hold a note at once**: the prepare pool's hands, one a hardware thread
    /// but one, and the main, ring, chain and compile threads beside them, on a desk processor of 32
    /// hardware threads and of nearly twice that. A thread past them notes nothing until one of them
    /// ends, which gives its slot back, and the report counts the threads that met the table full.
    /// The table travels as one annotation, whose value Crashpad holds to 20 KiB, which 64 slots fill
    /// to 17 KiB.
    inline constexpr std::size_t sNoteThreads = 64;

    /// One thread's last note, as a report reads it.
    struct NoteCopy
    {
        /// Terminated.
        char mText[sNoteCapacity];

        /// The system's id of the thread that wrote it, as `Platform::Process::currentThreadId` gives it.
        std::uint64_t mThread;

        /// Whether the text is one note, and not a note half written over by the next.
        bool mWhole;
    };

    /// Everything the note table says, as a report reads it.
    struct NotesRead
    {
        /// Why, where the code that ended the process said: `std::terminate`'s exception, the reason
        /// given to `Crash::fatal`. Terminated, and empty for a fault.
        char mReason[sNoteCapacity] = {};

        /// The thread the report is about, as `Platform::Process::currentThreadId` gives it: the one
        /// that faulted, or for a hang the one that draws. Nought where none is known.
        std::uint64_t mThread = 0;

        /// Threads that found every slot taken and noted nothing, each counted once.
        std::uint32_t mRefused = 0;

        std::size_t mCount = 0;

        /// `mThread`'s first, then the rest in the table's order.
        NoteCopy mNotes[sNoteThreads];
    };

    /// One thread's slot in the table, the note's file's own.
    struct NoteSlot;

    /// **What the calling thread is doing, for as long as a scope lasts**, for a report to name, on
    /// the end of what the thread's note already says: `outer > text`. The scope's end puts the note
    /// back as it found it, so a note never outlives the work it names, and a crash inside nested
    /// work reads as the path to it. Where the thread holds no slot, it notes nothing.
    ///
    /// **A fixed table, read from outside.** The monitor reads it out of the crashed process, as
    /// it reads the stacks, so it never depends on the crashed process to hand it over. A scope
    /// allocates nothing — what it found is kept in the scope, a note's bytes at most — so a loop
    /// may state each thing it works on. A thread has its own slot, because workers read models
    /// while the render thread uploads textures.
    ///
    /// **Ends on the thread it began on**, whose slot it holds and puts back: one kept as a member and
    /// destroyed on another thread is a broken contract, which a debug build stops at.
    class NoteScope
    {
    public:
        explicit NoteScope(std::string_view text);

        /// The text `std::format` makes of `format` and `arguments`, formatted on the stack and cut
        /// to what a note holds. A subject goes in quotes: `"reading the model \"{}\""`.
        template <class... Arguments>
            requires(sizeof...(Arguments) > 0)
        explicit NoteScope(std::format_string<Arguments...> format, Arguments&&... arguments)
        {
            char text[sNoteCapacity];
            const auto written
                = std::format_to_n(text, sNoteCapacity - 1, format, std::forward<Arguments>(arguments)...);
            begin(std::string_view(text, static_cast<std::size_t>(written.out - text)));
        }

        ~NoteScope();

        NoteScope(const NoteScope&) = delete;
        NoteScope& operator=(const NoteScope&) = delete;
        NoteScope(NoteScope&&) = delete;
        NoteScope& operator=(NoteScope&&) = delete;

    private:
        void begin(std::string_view text);

        /// The slot the note went into, or null where the thread holds none.
        NoteSlot* mSlot = nullptr;
        std::size_t mFoundLength = 0;
        char mFound[sNoteCapacity];
    };

    /// What the calling thread's note says now — the path `NoteScope`s made, `outer > inner` — or
    /// nothing where the thread holds no slot. Valid until the thread's next note changes.
    std::string_view ownNote();

    /// **Says why the process ends**, for the crash it ends on: the first call's `reason` stands, cut
    /// to what a note holds, and a later one, from a second thread failing at the same time, changes
    /// nothing. Allocates nothing and locks nothing.
    void endingReason(std::string_view reason);

    /// The table's bytes where they lie, which the catcher hands Crashpad as one annotation, so every
    /// dump carries them.
    std::span<const std::byte> noteTable();

    /// The name of the annotation the table travels as, which the monitor and `omw crash` read it by.
    inline constexpr std::string_view sNotesAnnotation = "openmw-notes";

    /// Reads a copy of `noteTable()`'s bytes, taken while no thread of the process that wrote them
    /// runs, with `thread`'s note first: the one that faulted, or for a hang the one that draws. A
    /// thread whose note is empty is doing nothing it noted, and is left out. The copy must be the
    /// whole table, since the monitor is this same executable and knows its layout; another size is
    /// no table, and reads as nothing noted.
    void readNotes(std::span<const std::byte> table, std::uint64_t thread, NotesRead& into);
}
