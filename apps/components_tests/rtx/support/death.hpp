#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashsummary.hpp>
#include <components/platform/process.hpp>

namespace Rtx::Testing
{
    /// What a death test's child finds in its environment, which is how it knows it is one.
    ///
    /// **gtest runs the whole binary again in the child**, its global set-up included, and says so
    /// only through a flag of its own that it tells user code not to read. A set-up that builds what
    /// every test shares — `rtx-gpu-tests`'s renderer, three and a half seconds — would build it
    /// again for a statement that aborts. `expectDies` sets this before its child starts; the parent
    /// keeps it too and never reads it again, having set itself up before any test ran.
    inline constexpr const char* sDeathChildVariable = "RTX_TESTS_DEATH_CHILD";

    /// Whether this process is a death test's child, `sDeathChildVariable`.
    inline bool inDeathChild()
    {
        const char* const value = std::getenv(sDeathChildVariable);
        return value != nullptr && std::string_view(value) == "1";
    }

    /// Expects `statement` to end the process with `message`, the way a failed `assert` does.
    ///
    /// **The child leaves no core**, by `Platform::Process::disableCoreDump`, and the binary itself
    /// still leaves one where it really crashes.
    ///
    /// **An uncaught exception says what the crash catcher's report would**, `Crash::terminateReason`,
    /// on the standard error the test reads. There is no catcher here, and MSVC's own terminate
    /// handler aborts without a word where libstdc++'s names the exception.
    ///
    /// @param where the caller's own line, never passed, so a failure reports there.
    inline void expectDies(const std::function<void()>& statement, std::string_view message,
        std::source_location where = std::source_location::current())
    {
        const ::testing::ScopedTrace trace(where.file_name(), static_cast<int>(where.line()), "expectDies");

        Platform::Process::setEnvironment(sDeathChildVariable, "1");

        EXPECT_DEATH(
            {
                Platform::Process::disableCoreDump();
                std::set_terminate([] {
                    std::fprintf(stderr, "%s\n", Crash::terminateReason().c_str());
                    std::abort();
                });
                statement();
            },
            std::string(message));
    }

    /// Whether this build keeps `assert`: the one place a test asks, so a test that checks an
    /// assert compiles in every build.
#ifdef NDEBUG
    inline constexpr bool sAssertsOn = false;
#else
    inline constexpr bool sAssertsOn = true;
#endif

    /// `expectDies` for a contract only `assert` checks. Where asserts are off there is nothing to
    /// die of: the statement is not run, as a death test never runs it in this process either, and
    /// the test is marked skipped, so a build without asserts does not report the check as made.
    inline void expectAssertDies(const std::function<void()>& statement, std::string_view message,
        std::source_location where = std::source_location::current())
    {
        if constexpr (!sAssertsOn)
            GTEST_SKIP() << "asserts are off, so " << message << " is not checked";
        else
            expectDies(statement, message, where);
    }
}
