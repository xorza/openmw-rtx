#include "crashtestssystem.hpp"

#include <cstring>

#include <intrin.h>

namespace CrashTests
{
    namespace
    {
        struct Base
        {
            Base() { call(); }
            virtual ~Base() = default;

            // Out of line, so the call the constructor makes is a virtual one the compiler cannot
            // resolve to the pure function and refuse.
            __declspec(noinline) void call() { pure(); }

            virtual void pure() = 0;
        };

        struct Derived : Base
        {
            void pure() override {}
        };
    }

    Raised raisedOnThisSystem()
    {
        return Raised{
            .mFault = { "EXCEPTION_ACCESS_VIOLATION reading 0x10" },
            .mOverflow = { "EXCEPTION_STACK_OVERFLOW" },
            .mIllegal = { "EXCEPTION_ILLEGAL_INSTRUCTION" },
            .mStackScanned = true,
        };
    }

    void addModesOfThisSystem(std::vector<Mode>& into, std::string_view crashed)
    {
        into.push_back({ "abort", "Crash: abort()", {}, {}, true, crashed });
        into.push_back({ "pure-call", "Crash: a pure virtual function was called", {}, {}, true, crashed });
        into.push_back(
            { "invalid-parameter", "Crash: the C runtime was given an invalid parameter", {}, {}, true, crashed });
    }

    std::optional<int> runModeOfThisSystem(std::string_view mode)
    {
        if (mode == "pure-call")
        {
            Derived derived;
            return 0;
        }
        if (mode == "invalid-parameter")
        {
            char into[1];
            strcpy_s(into, sizeof(into), "longer than one");
            return 0;
        }
        return std::nullopt;
    }

    void illegalInstruction()
    {
        __ud2();
    }
}
