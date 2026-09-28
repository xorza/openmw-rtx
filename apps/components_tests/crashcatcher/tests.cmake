if (OPENMW_CRASHPAD)
    # Every way a game ends, each in a process of its own with the real catcher: not a gtest
    # binary, because each mode ends the process that runs it. `cmake/Tests.cmake` runs its matrix.
    # With the half that knows the system it runs on, so the shared half never asks.
    if (WIN32)
        set(CRASH_TESTS_SYSTEM crashcatcher/crashtestswin32.cpp)
    else()
        set(CRASH_TESTS_SYSTEM crashcatcher/crashtestsposix.cpp)
    endif()
    openmw_add_executable(crash-tests crashcatcher/crashtests.cpp crashcatcher/crashtestssystem.hpp ${CRASH_TESTS_SYSTEM})
    target_link_libraries(crash-tests components)
endif()
