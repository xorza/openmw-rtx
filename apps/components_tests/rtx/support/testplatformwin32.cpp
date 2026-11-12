#include "testplatform.hpp"

#include <cstddef>

#include <Windows.h>
#include <malloc.h>

namespace Rtx::Testing::TestPlatform
{
    void* allocateAligned(const std::size_t size, const std::size_t alignment) noexcept
    {
        return _aligned_malloc(size == 0 ? 1 : size, alignment);
    }

    void freeAligned(void* memory) noexcept
    {
        _aligned_free(memory);
    }

    // A crash on Windows leaves a dump only where something asks for one, and the test binaries ask
    // for none.
    void disableCoreDump() {}

    bool showsADesktop()
    {
        USEROBJECTFLAGS flags{};
        DWORD needed = 0;
        // Where the station cannot be asked, the tests that present run and say what failed.
        if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &needed))
            return true;
        return (flags.dwFlags & WSF_VISIBLE) != 0;
    }
}
