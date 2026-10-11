#include "crashsummary.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <components/files/conversion.hpp>

#include "crash.hpp"

namespace Crash
{
    namespace
    {
        /// Whether `left` and `right` spell the same character of a path: either separator for
        /// the other, and an ASCII letter in either case, as Windows reads a path.
        bool samePathCharacter(const char left, const char right)
        {
            const auto folded = [](const char c) {
                if (c == '\\')
                    return '/';
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            };
            return folded(left) == folded(right);
        }

        /// Whether `c` carries on the name of a folder, so a match of the home folder that `c`
        /// follows is a longer name and not the folder: `C:/Users/x` in `C:/Users/xavier`.
        bool continuesName(const char c)
        {
            const auto byte = static_cast<unsigned char>(c);
            return byte >= 0x80 || std::isalnum(byte) != 0 || c == '_' || c == '-' || c == '.';
        }

        /// `text` with every spelling of `home` in it written `~`. Nothing for an empty `home`.
        void nameHome(std::string& text, std::string_view home)
        {
            while (!home.empty() && (home.back() == '/' || home.back() == '\\'))
                home.remove_suffix(1);
            if (home.empty())
                return;

            std::string named;
            std::size_t at = 0;
            while (at < text.size())
            {
                const std::size_t end = at + home.size();
                if (end <= text.size() && std::equal(home.begin(), home.end(), text.begin() + at, samePathCharacter)
                    && (end == text.size() || !continuesName(text[end])))
                {
                    named += '~';
                    at = end;
                }
                else
                    named += text[at++];
            }
            text = std::move(named);
        }

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

        /// What the thread the report is about did, as its note says: the one that faulted, the one
        /// that asked, or the one that draws, which a hang stopped.
        std::string_view markOf(ReportKind kind)
        {
            switch (kind)
            {
                case ReportKind::Crash:
                    return ", which crashed";
                case ReportKind::Hang:
                    return ", which draws";
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

    std::string title(const CrashFacts& facts, const SummaryReaders& readers)
    {
        std::string heading = titleOf(facts, kindPrefix(facts), reasonOf(facts));
        if (readers.mPublic)
            nameHome(heading, readers.mHome);
        return heading;
    }

    void summarise(const CrashFacts& facts, std::vector<std::string>& lines, const SummaryReaders& readers)
    {
        const std::size_t first = lines.size();
        const NotesRead& notes = facts.mNotes;
        const std::string kind = kindPrefix(facts);
        const std::string reason = reasonOf(facts);

        std::string headline = titleOf(facts, kind, reason);
        if (notes.mThread != 0)
            headline += " in thread " + std::to_string(notes.mThread);
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
                + std::string(note.mThread == notes.mThread ? markOf(notes.mKind) : "") + ": " + note.mText
                + (note.mWhole ? "" : " (half written)"));
        }
        if (notes.mCount == 0)
            lines.push_back(kind + "no thread noted anything");

        for (const auto& [key, value] : facts.mAnnotations)
            lines.push_back(kind + key + ": " + value);

        if (facts.mDump.empty())
            lines.push_back(kind + "no dump was written");
        else if (readers.mPublic)
            lines.push_back(
                kind + "dump " + Files::pathToUnicodeString(Files::pathFromUnicodeString(facts.mDump).filename()));
        else
            lines.push_back(kind + "dump " + facts.mDump);

        if (readers.mPublic)
            for (std::string& line : std::span(lines).subspan(first))
                nameHome(line, readers.mHome);
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
