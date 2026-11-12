# The suites CTest runs. The label `device` marks what needs a GPU. Tests run where the binaries are,
# because the ones that read game data resolve `./resources` as the tool does.
enable_testing()

# The test binaries' own files at `-Og` on Clang, the code they test at the build's flags: their objects
# build 30% faster for runs 10% slower (`docs/rtx/research/2026-11-11-test-binaries-at-og.md`). MSVC's
# are at the build's, as its `/Od` was not measured. Each test binary links it where it is defined.
add_library(openmw-test-options INTERFACE)
target_compile_options(openmw-test-options INTERFACE $<$<CXX_COMPILER_ID:Clang>:-Og>)

function(openmw_add_test name target)
    cmake_parse_arguments(PARSE_ARGV 2 TEST "" "" "LABELS;ARGS")
    if(NOT TARGET ${target})
        message(FATAL_ERROR "The test ${name} runs ${target}, which the build does not define")
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
endforeach()

# The benchmarks once each, as a smoke run: Google Benchmark's dry run takes one iteration and measures
# nothing, so its warnings about timings (ASLR, a debug library) do not apply, and CTest shows its output on
# failure alone. `omw release bench` is what measures.
foreach(
    benchmark
    IN
    ITEMS
        openmw_detournavigator_navmeshtilescache_benchmark
        openmw_esm_refid_benchmark
        openmw_settings_access_benchmark
)
    openmw_add_test(${benchmark} ${benchmark} ARGS --benchmark_dry_run)
endforeach()
