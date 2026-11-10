# The tree's own checks: the warnings each compiler takes and, where a preset asks, warnings as errors.
# One `INTERFACE` target that every target the tree defines under `components/` and `apps/` links
# (`openmw_link_checks`), and no third-party one does: a directory's `add_compile_options` reached a
# vendored target or not by the order its folder was added in.

# The compilers the tree is built and checked with, and none older: GCC 15, Ubuntu 26.04's, Clang 23, the
# clang job's and the editor's, and MSVC 19.44, Visual Studio 2022 17.14's. An older one stops here, and not
# inside a bug a workaround below was dropped for.
set(openmw_compiler_floors GNU 15 Clang 23 MSVC 19.44)
list(FIND openmw_compiler_floors "${CMAKE_CXX_COMPILER_ID}" at)
if (at EQUAL -1)
    message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} is no compiler the tree is built with: GCC, Clang or MSVC")
endif ()
math(EXPR at "${at} + 1")
list(GET openmw_compiler_floors ${at} floor)
if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS floor)
    message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is older than ${floor}, the oldest the tree is built with")
endif ()

add_library(openmw-checks INTERFACE)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(openmw-checks INTERFACE
        /W4
        /wd4100 # Unreferenced formal parameter (-Wunused-parameter)
        /wd4127 # Conditional expression is constant
        /wd4996 # Function was declared deprecated
        /wd5054 # Deprecated operations between enumerations of different types caused by Qt headers
        /wd4244 # Implicit narrowing between arithmetic types (-Wconversion, which GCC's -Wall -Wextra leave out)
        /wd4267 # Implicit narrowing of a size_t (the same)
        /w14855 # implicit capture of 'this' via '[=]' is deprecated in 'version'
        $<$<BOOL:${OPENMW_MSVC_WERROR}>:/WX>
    )
else ()
    target_compile_options(openmw-checks INTERFACE
        -Wall -Wextra -Wundef -Wextra-semi -Wno-unused-parameter -pedantic -Wnon-virtual-dtor -Wshadow
        # Beyond upstream's. Rejected: `-Wfloat-equal`, for deliberate sentinels, and `-Wold-style-cast` and
        # `-Wuseless-cast`, which fire inside OpenSceneGraph's and MyGUI's headers.
        -Wnull-dereference -Wcast-qual
        $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>
        $<$<COMPILE_LANGUAGE:CXX>:-Wzero-as-null-pointer-constant>
    )

    if (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
        target_compile_options(openmw-checks INTERFACE -Wno-potentially-evaluated-expression)
    endif ()

    if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
        target_compile_options(openmw-checks INTERFACE
            # GCC's alone: Clang's check of that name takes every implicit promotion, not arithmetic's
            # only, and fired 614 times in a tree GCC's passes.
            -Wdouble-promotion
            -Wno-unused-but-set-parameter -Wduplicated-branches -Wduplicated-cond -Wlogical-op
            # GCC bug 105438, which GCC 15.2 still reports in sol's `stack_field.hpp`, a string literal's
            # bounds against another's length.
            -Wno-array-bounds
        )

        # Wrong before GCC 16: `-Wmaybe-uninitialized` reports every copy of a `std::optional` (GCC bug
        # 80635), Ubuntu 26.04's GCC 15.2 among them, and `-Wnull-dereference` OSG's inline
        # `pushOntoNodePath`.
        if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
            target_compile_options(openmw-checks INTERFACE -Wno-maybe-uninitialized -Wno-null-dereference)
        endif ()

        # Wrong on GCC 16: `-Wstringop-overflow` reports a byte vector's `resize(n, value)` at `-O3` as an
        # overflow no path reaches (GCC bugs 107852, 117983, 118521), and came back after a call site was
        # rewritten around it.
        if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
            target_compile_options(openmw-checks INTERFACE -Wno-stringop-overflow)
        endif ()
    endif ()
endif ()

# What a preset adds to the checks, `-Werror` on Linux.
if (OPENMW_CXX_FLAGS)
    separate_arguments(openmw_cxx_flags NATIVE_COMMAND "${OPENMW_CXX_FLAGS}")
    target_compile_options(openmw-checks INTERFACE ${openmw_cxx_flags})
endif ()

# Links `openmw-checks` into every target a folder under `components/` or `apps/` defines, from
# `directory` down, but the interface and custom ones, which compile nothing, and one that compiles a
# third party's source and says so with `OPENMW_CHECKS OFF`. Every folder is walked, as
# `components/rtxvulkan` is added from the root, and one outside those two, `extern/` or a fetched
# project, is a third party's. Through the target's `LINK_LIBRARIES`, the property
# `target_link_libraries` fills, so a target linked with either of its signatures takes it alike.
function (openmw_link_checks directory)
    file(RELATIVE_PATH folder "${PROJECT_SOURCE_DIR}" "${directory}")
    if (folder MATCHES "^(components|apps)(/|$)")
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach (target IN LISTS targets)
            get_target_property(type ${target} TYPE)
            get_target_property(checks ${target} OPENMW_CHECKS)
            if (NOT type MATCHES "^(INTERFACE_LIBRARY|UTILITY)$" AND NOT checks STREQUAL "OFF")
                set_property(TARGET ${target} APPEND PROPERTY LINK_LIBRARIES openmw-checks)
            endif ()
        endforeach ()
    endif ()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach (subdirectory IN LISTS subdirectories)
        openmw_link_checks("${subdirectory}")
    endforeach ()
endfunction ()
