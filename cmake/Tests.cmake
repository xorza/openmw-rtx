# The suites CTest runs. The label `device` marks what needs a GPU. Tests run where the binaries are,
# because the ones that read game data resolve `./resources` as the tool does.
enable_testing()

# Options for the test binaries alone, where a preset names any: at `-Og` a test file builds twice as
# fast, and the code it tests keeps the build's own flags.
set(OPENMW_TEST_COMPILE_OPTIONS "" CACHE STRING "Compile options added to the test binaries alone")

function(openmw_add_test name target)
    cmake_parse_arguments(TEST "" "" "LABELS;ARGS" ${ARGN})
    if (NOT TARGET ${target})
        return()
    endif()
    # Once per binary, which a sharded suite registers once per shard.
    get_target_property(optioned ${target} OPENMW_TEST_OPTIONED)
    if (OPENMW_TEST_COMPILE_OPTIONS AND NOT optioned)
        target_compile_options(${target} PRIVATE ${OPENMW_TEST_COMPILE_OPTIONS})
        set_target_properties(${target} PROPERTIES OPENMW_TEST_OPTIONED ON)
    endif()
    add_test(NAME ${name} COMMAND ${target} ${TEST_ARGS} WORKING_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}")
    # The target, by name, for whatever builds before it runs: CTest names no command for a test
    # whose binary is not built yet.
    set_tests_properties(${name} PROPERTIES LABELS "${TEST_LABELS}" OPENMW_TARGET ${target})
endfunction()

openmw_add_test(components components-tests)
openmw_add_test(openmw openmw-tests)
openmw_add_test(cs openmw-cs-tests)

# Every way a game ends, each in a process of its own with the real catcher. The reports stay in the
# build after a run, so a failed mode leaves what it wrote; each mode empties its own folder first.
openmw_add_test(crash.matrix crash-tests ARGS --matrix "${RUNTIME_OUTPUT_DIRECTORY}/crash-matrix")

# Two processes on one device, so one's host work overlaps the other's device work: 17 s rather than
# 23, and three shards were no faster than two.
foreach (shard RANGE 1)
    openmw_add_test(rtx.gpu.${shard} rtx-gpu-tests LABELS device)
    if (TEST rtx.gpu.${shard})
        set_tests_properties(rtx.gpu.${shard} PROPERTIES ENVIRONMENT "GTEST_TOTAL_SHARDS=2;GTEST_SHARD_INDEX=${shard}")
    endif()
endforeach()
