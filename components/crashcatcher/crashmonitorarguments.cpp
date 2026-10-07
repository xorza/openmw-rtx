#include "crashmonitorarguments.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <variant>

#include <components/files/conversion.hpp>

namespace Crash
{
    namespace
    {
        constexpr std::string_view sClient = "--openmw-client";
        constexpr std::string_view sNotes = "--openmw-notes";
        constexpr std::string_view sApplication = "--openmw-application";
        constexpr std::string_view sIssues = "--openmw-issues";

        /// Who answers: `player`, `nobody`, or `end-after-<milliseconds>`.
        constexpr std::string_view sAnswering = "--openmw-answering";
        constexpr std::string_view sEndAfter = "end-after-";

        std::string spell(const Answering& answering)
        {
            if (const EndAfter* const after = std::get_if<EndAfter>(&answering))
                return std::string(sEndAfter) + std::to_string(after->mDelay.count());
            return std::holds_alternative<AskNobody>(answering) ? "nobody" : "player";
        }

        /// What `spell` wrote, read back; the player for what it never writes, so a monitor of
        /// another build asks rather than ends.
        Answering answeringOf(std::string_view spelled)
        {
            if (spelled == "nobody")
                return AskNobody{};
            if (spelled.starts_with(sEndAfter))
                return EndAfter{ std::chrono::milliseconds(
                    std::strtoll(std::string(spelled.substr(sEndAfter.size())).c_str(), nullptr, 10)) };
            return AskThePlayer{};
        }
        constexpr std::string_view sDatabase = "--database";

        std::string option(std::string_view name, std::string_view value)
        {
            return std::string(name) + "=" + std::string(value);
        }

        /// The value of `--name=value` in `argument`, where it is that option.
        std::optional<std::string_view> valueOf(std::string_view argument, std::string_view name)
        {
            if (!argument.starts_with(name) || argument.size() <= name.size() || argument[name.size()] != '=')
                return std::nullopt;
            return argument.substr(name.size() + 1);
        }
    }

    std::vector<std::string> MonitorArguments::write() const
    {
        char notes[24];
        std::snprintf(notes, sizeof(notes), "0x%llx", static_cast<unsigned long long>(mNotes));

        std::vector<std::string> written{
            std::string(sMonitorSwitch),
            option(sClient, std::to_string(mClient)),
            option(sNotes, notes),
            option(sApplication, mApplication),
            option(sIssues, mIssues),
            option(sAnswering, spell(mAnswering)),
        };

        return written;
    }

    MonitorArguments MonitorArguments::read(std::span<const std::string> arguments, std::vector<std::string>& handler)
    {
        MonitorArguments read;
        handler.clear();
        for (const std::string& argument : arguments)
        {
            if (argument == sMonitorSwitch)
                continue;

            if (const auto client = valueOf(argument, sClient))
                read.mClient = static_cast<std::uint32_t>(std::strtoul(std::string(*client).c_str(), nullptr, 10));
            else if (const auto notes = valueOf(argument, sNotes))
            {
                // An address that does not read as a number to its end is no table at all.
                const std::string text(*notes);
                char* end = nullptr;
                read.mNotes = std::strtoull(text.c_str(), &end, 16);
                if (end == text.c_str() || *end != '\0')
                    read.mNotes = 0;
            }
            else if (const auto application = valueOf(argument, sApplication))
                read.mApplication = *application;
            else if (const auto issues = valueOf(argument, sIssues))
                read.mIssues = *issues;
            else if (const auto answering = valueOf(argument, sAnswering))
                read.mAnswering = answeringOf(*answering);
            else
            {
                if (const auto database = valueOf(argument, sDatabase))
                    read.mDatabase = Files::pathFromUnicodeString(*database);
                handler.push_back(argument);
            }
        }
        return read;
    }
}
