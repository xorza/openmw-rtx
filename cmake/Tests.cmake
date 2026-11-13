# The suites CTest runs. The label `device` marks what needs a GPU. Tests run where the binaries are,
# because the ones that read game data resolve `./resources` as the tool does.
enable_testing()

# The test binaries' own files at `-Og` on Clang, the code they test at the build's flags: their objects
# build 30% faster for runs 10% slower (`docs/research/2026-11-11-test-binaries-at-og.md`). MSVC's
# are at the build's, as its `/Od` was not measured. Each test binary links it where it is defined.
add_library(openmw-test-options INTERFACE)
target_compile_options(openmw-test-options INTERFACE $<$<CXX_COMPILER_ID:Clang>:-Og>)

function(openmw_add_test name target)
    cmake_parse_arguments(PARSE_ARGV 2 TEST "" "TIMEOUT" "LABELS;ARGS")
    if(NOT TARGET ${target})
        message(FATAL_ERROR "The test ${name} runs ${target}, which the build does not define")
    endif()
    # A target a test runs is no part of `all`: `omw build` and `omw test` name the test targets
    # (`Build.build_default`), and the gate's release compile, which builds `all` and runs nothing, builds
    # the programs alone (`gate._release_compile` says what that saves). LLVM keeps its unit tests out of
    # `all` the same way (`LLVM_BUILD_TESTS`). No test target is installed, which is the one thing CMake
    # warns an excluded target about.
    set_target_properties(
        ${target}
        PROPERTIES
            EXCLUDE_FROM_ALL
                ON
    )
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
    # The property and not `--timeout`, which sets only the limit of a test that states none.
    if(DEFINED TEST_TIMEOUT)
        set_property(
            TEST
                ${name}
            PROPERTY
                TIMEOUT
                    ${TEST_TIMEOUT}
        )
    endif()
endfunction()

# A binary in `count` processes started together, each a share of its tests by their index: Google
# Test runs each test once across the shards (`GTEST_TOTAL_SHARDS`). The keywords are
# `openmw_add_test`'s, stated here as well so the formatter reads a call of this as it reads one of that.
function(openmw_add_test_shards name target count)
    cmake_parse_arguments(PARSE_ARGV 3 SHARDS "" "TIMEOUT" "LABELS;ARGS")
    math(EXPR last "${count} - 1")
    foreach(shard RANGE ${last})
        openmw_add_test(
            ${name}.${shard}
            ${target}
            LABELS
                ${SHARDS_LABELS}
            ARGS
                ${SHARDS_ARGS}
            TIMEOUT ${SHARDS_TIMEOUT}
        )
        set_property(
            TEST
                ${name}.${shard}
            APPEND
            PROPERTY
                ENVIRONMENT
                    "GTEST_TOTAL_SHARDS=${count};GTEST_SHARD_INDEX=${shard}"
        )
    endforeach()
endfunction()

# In two: one process took 40 s beside the GPU binary's four, which made it the step's bound, where
# it takes 20 s alone.
openmw_add_test_shards(components components-tests 2)
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

# Four processes on one device, split by test index, so one's host work overlaps the others' device work. On a
# low-end AMD integrated GPU the binary ends in 35 s in four, against 48 s in two and 30 s in six, which buys
# four seconds for two more devices and starts (`docs/research/2026-11-13-gate-step-times.md`). A process's
# start is what bounds the count, two devices and two renderers: while it ran the layers' shader checks
# (`Testing::describeValidation`) a third process was no faster than two. By index and not by suite:
# `RtxVisibilityTest` is 208 of the 499 tests and over half their time, so a suite is no unit to balance by.
#
# Fifteen minutes and not the preset's five: the first run after a shader change compiles every pipeline, which
# passed 300 s on a low-end AMD integrated GPU under AMD's Windows driver, where the same binary with its pipelines
# in the cache took 74 s.
openmw_add_test_shards(rtx.gpu rtx-gpu-tests 4 LABELS device TIMEOUT 900)

# The benchmarks once each, as a smoke run: Google Benchmark's dry run takes one iteration and measures
# nothing, so its warnings about timings (ASLR, a debug library) do not apply, and CTest shows its output on
# failure alone. `omw release bench` is what measures.
foreach(
    benchmark
    IN
    ITEMS
        openmw_detournavigator_navmeshtilescache_benchmark
        openmw_esm_refid_benchmark
        openmw_rtx_chain_encode_benchmark
        openmw_settings_access_benchmark
)
    openmw_add_test(${benchmark} ${benchmark} ARGS --benchmark_dry_run)
endforeach()
