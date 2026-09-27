# The suites CTest runs, so the tests a build has are the build's own answer: `ctest` lists what this
# configuration made, and `omw test` asks for it by label rather than by a list of binaries kept
# beside the build.
#
# **Each suite that holds both upstream's tests and the fork's is two tests**, one filter the other's
# complement, so the fork's run alone in seconds and the two together are the binary whole. Labels:
# `fork` for what this fork wrote, `upstream` for upstream's, and `device` for what needs a GPU.
# Every test runs where the binaries are, because the tests that read game data resolve
# `./resources` as the tool does.
enable_testing()

function(openmw_add_test name target)
    cmake_parse_arguments(TEST "" "FILTER" "LABELS;ARGS" ${ARGN})
    if (NOT TARGET ${target})
        return()
    endif()
    set(filter)
    if (TEST_FILTER)
        set(filter "--gtest_filter=${TEST_FILTER}")
    endif()
    add_test(NAME ${name} COMMAND ${target} ${filter} ${TEST_ARGS} WORKING_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}")
    # The target, by name, for whatever builds before it runs: CTest names no command for a test
    # whose binary is not built yet.
    set_tests_properties(${name} PROPERTIES LABELS "${TEST_LABELS}" OPENMW_TARGET ${target})
endfunction()

set(OPENMW_FORK_TESTS "Rtx*:Sky*:Crash*")
if (OPENMW_RTX)
    openmw_add_test(components.fork components-tests FILTER "${OPENMW_FORK_TESTS}" LABELS fork)
    openmw_add_test(components.upstream components-tests FILTER "-${OPENMW_FORK_TESTS}" LABELS upstream)
    openmw_add_test(openmw.fork openmw-tests FILTER "Rtx*" LABELS fork)
    openmw_add_test(openmw.upstream openmw-tests FILTER "-Rtx*" LABELS upstream)
else()
    openmw_add_test(components components-tests LABELS upstream)
    openmw_add_test(openmw openmw-tests LABELS upstream)
endif()
openmw_add_test(cs openmw-cs-tests LABELS upstream)

# Every way a game ends, each in a process of its own with the real catcher. The reports stay in the
# build after a run, so a failed mode leaves what it wrote; each mode empties its own folder first.
openmw_add_test(crash.matrix crash-tests ARGS --matrix "${RUNTIME_OUTPUT_DIRECTORY}/crash-matrix" LABELS fork)

# Alone, because the device does its work one frame at a time and a second process only waits.
openmw_add_test(rtx.gpu rtx-gpu-tests LABELS fork device)
if (TEST rtx.gpu)
    set_tests_properties(rtx.gpu PROPERTIES RUN_SERIAL ON)
endif()
