# The suites CTest runs. The label `device` marks what needs a GPU. Tests run where the binaries are,
# because the ones that read game data resolve `./resources` as the tool does.
enable_testing()

# What the suites write, their times and the crash matrix's runs, which the driver reads from
# `omw-folders.json`.
set(RTX_TEST_OUTPUT_DIR "${PROJECT_BINARY_DIR}/test-output")

# The test binaries' own files at `-Og` on GCC and Clang, the code they test at the build's flags: their objects
# build 30% faster for runs 10% slower (`docs/rtx/research/2026-11-11-test-binaries-at-og.md`). MSVC's
# are at the build's, as its `/Od` was not measured.
set(openmw_test_compile_options $<$<CXX_COMPILER_ID:GNU,Clang>:-Og>)

function(openmw_add_test name target)
    cmake_parse_arguments(PARSE_ARGV 2 TEST "" "" "LABELS;ARGS")
    if(NOT TARGET ${target})
        return()
    endif()
    # Once per binary, which a sharded suite registers once per shard.
    get_target_property(optioned ${target} OPENMW_TEST_OPTIONED)
    if(NOT optioned)
        target_compile_options(${target} PRIVATE ${openmw_test_compile_options})
        set_target_properties(
            ${target}
            PROPERTIES
                OPENMW_TEST_OPTIONED
                    ON
        )
    endif()
    add_test(
        NAME ${name}
        COMMAND
            ${target} ${TEST_ARGS}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
    )
    # The target, by name, for whatever builds before it runs: CTest names no command for a test
    # whose binary is not built yet. Each test's times go to a report named after the test rather
    # than the binary: two shards of one binary would otherwise both pick the same free name.
    set_tests_properties(
        ${name}
        PROPERTIES
            LABELS
                "${TEST_LABELS}"
            OPENMW_TARGET
                ${target}
            ENVIRONMENT
                "GTEST_OUTPUT=json:${RTX_TEST_OUTPUT_DIR}/times/${name}.json"
    )
endfunction()

openmw_add_test(components components-tests)
openmw_add_test(openmw openmw-tests)
openmw_add_test(cs openmw-cs-tests)

# Every way a game ends, each in a process of its own with the real catcher. The reports stay in the
# build after a run, so a failed mode leaves what it wrote; each mode empties its own folder first.
openmw_add_test(
    crash.matrix
    crash-tests
    ARGS
        --matrix
        "${RTX_TEST_OUTPUT_DIR}/crash-matrix"
)

# Two processes on one device, so one's host work overlaps the other's device work: 17 s rather than
# 23, and three shards were no faster than two. Fifteen minutes and not the preset's five: the first
# run after a shader change compiles every pipeline, which passed 300 s on a low-end AMD integrated
# GPU under AMD's Windows driver, where the same binary with its pipelines in the cache took 74 s.
# The property and not `--timeout`, which sets only the limit of a test that states none.
foreach(shard RANGE 1)
    openmw_add_test(rtx.gpu.${shard} rtx-gpu-tests LABELS device)
    if(TEST rtx.gpu.${shard})
        set_property(
            TEST
                rtx.gpu.${shard}
            APPEND
            PROPERTY
                ENVIRONMENT
                    "GTEST_TOTAL_SHARDS=2;GTEST_SHARD_INDEX=${shard}"
        )
        set_property(
            TEST
                rtx.gpu.${shard}
            PROPERTY
                TIMEOUT
                    900
        )
    endif()
endforeach()
