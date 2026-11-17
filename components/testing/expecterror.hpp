#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace Testing
{
    /// Fails the test unless `call` throws an exception whose message holds `part`, the failure placed where it was
    /// asked for. `ADD_FAILURE` and not `FAIL`, which only a test's own body may return through.
    inline void expectError(const std::function<void()>& call, std::string_view part,
        const std::source_location place = std::source_location::current())
    {
        const ::testing::ScopedTrace trace(
            place.file_name(), static_cast<std::int32_t>(place.line()), "expected an error");
        try
        {
            call();
        }
        catch (const std::exception& e)
        {
            EXPECT_THAT(e.what(), ::testing::HasSubstr(std::string(part)));
            return;
        }
        ADD_FAILURE() << "Expected error";
    }
}
