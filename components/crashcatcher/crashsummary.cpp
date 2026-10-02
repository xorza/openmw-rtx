#include "crashsummary.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "crash.hpp"

namespace Crash
{
    namespace
    {
        std::string_view kindOf(ReportKind kind)
        {
            switch (kind)
            {
                case ReportKind::Crash:
                    return "Crash";
                case ReportKind::Hang:
                    return "Hang";
                case ReportKind::Report:
                    return "Report";
            }

            fatal("a report kind the summary does not name");
        }

        /// What the thread that raised the report did, as its note says: the one that faulted, or
        /// the one that asked. A hang's is whichever thread took the monitor's request, which says
        /// nothing, so it is not marked.
        std::string_view markOf(ReportKind kind)
        {
            switch (kind)
            {
                case ReportKind::Crash:
                    return ", which crashed";
                case ReportKind::Hang:
                    return "";
                case ReportKind::Report:
                    return ", which asked";
            }

            fatal("a report kind the summary does not mark");
        }

        std::string kindPrefix(const CrashFacts& facts)
        {
            return std::string(kindOf(facts.mNotes.mKind)) + ": ";
        }

        std::string reasonOf(const CrashFacts& facts)
        {
            // The game gives no reason for a hang, which only its monitor saw.
            return facts.mNotes.mKind == ReportKind::Hang
                ? "no frame for " + std::to_string(facts.mStalledFor) + " seconds"
                : std::string(facts.mNotes.mReason);
        }

        std::string titleOf(const CrashFacts& facts, const std::string& kind, const std::string& reason)
        {
            // The reason first where the code that asked gave one, because it is the reason the
            // exception was raised: `std::terminate` raises its own, which names nothing of its cause.
            std::string title = kind;
            if (!reason.empty())
                title += reason;
            else if (!facts.mException.empty())
                title += facts.mException;
            else
                title += "no exception was recorded";
            return title;
        }
    }

    std::string_view terminateReason(const std::span<char, sNoteCapacity> into)
    {
        const auto written
            = [&]<class... Arguments>(std::format_string<Arguments...> format, Arguments&&... arguments) {
                  const auto end
                      = std::format_to_n(into.data(), into.size() - 1, format, std::forward<Arguments>(arguments)...);
                  return std::string_view(into.data(), end.out);
              };

        const std::exception_ptr current = std::current_exception();
        if (!current)
            return written("std::terminate");

        try
        {
            std::rethrow_exception(current);
        }
        catch (const std::exception& error)
        {
            return written("std::terminate on an uncaught exception: {}", error.what());
        }
        catch (...)
        {
            return written("std::terminate on an uncaught exception that is no std::exception");
        }
    }

    std::string title(const CrashFacts& facts)
    {
        return titleOf(facts, kindPrefix(facts), reasonOf(facts));
    }

    void summarise(const CrashFacts& facts, std::vector<std::string>& lines)
    {
        const NotesRead& notes = facts.mNotes;
        const std::string kind = kindPrefix(facts);
        const std::string reason = reasonOf(facts);

        std::string headline = titleOf(facts, kind, reason);
        if (facts.mThread != 0)
            headline += " in thread " + std::to_string(facts.mThread);
        lines.push_back(std::move(headline));

        if (!reason.empty() && !facts.mException.empty())
            lines.push_back(kind + "raised as " + facts.mException);

        if (!facts.mWhere.empty())
            lines.push_back(kind + "at " + facts.mWhere);

        if (!facts.mStack.empty())
        {
            std::string stack = kind + "return addresses on its stack:";
            for (const std::string& address : facts.mStack)
                stack += " " + address;
            lines.push_back(std::move(stack));
        }

        for (std::size_t i = 0; i < notes.mCount; ++i)
        {
            const NoteCopy& note = notes.mNotes[i];
            lines.push_back(kind + "note of thread " + std::to_string(note.mThread)
                + std::string(note.mThread == facts.mThread ? markOf(notes.mKind) : "") + ": " + note.mText
                + (note.mWhole ? "" : " (half written)"));
        }
        if (notes.mCount == 0)
            lines.push_back(kind + "no thread noted anything");

        for (const auto& [key, value] : facts.mAnnotations)
            lines.push_back(kind + key + ": " + value);

        lines.push_back(kind + (facts.mDump.empty() ? "no dump was written" : "dump " + facts.mDump));
    }

    std::string hex(const std::uint64_t value)
    {
        return std::format("{:#x}", value);
    }

    std::string nameOf(const CodeNames names, const std::uint32_t code, std::string otherwise)
    {
        const auto named = std::ranges::find(names, code, &std::pair<std::uint32_t, std::string_view>::first);
        return named != names.end() ? std::string(named->second) : std::move(otherwise);
    }
}
