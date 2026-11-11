# What the tree's targets compile with, as two `INTERFACE` targets `openmw_link_tree` links: `openmw-system`,
# how every target the tree defines compiles on its system, the vendored ones under `extern/` included;
# and `openmw-checks`, the warnings each compiler takes,
# for the tree's own under `components/` and `apps/` and no third party's. A directory's
# `add_compile_options` reached a target or not by the order its folder was added in.

# The compilers the tree is built and checked with, and none older: GCC 15, Ubuntu 26.04's, Clang 23, the
# clang job's and the editor's, and MSVC 19.44, Visual Studio 2022 17.14's. An older one stops here, and not
# inside a bug a workaround below was dropped for.
set(openmw_compiler_floors
    GNU
    15
    Clang
    23
    MSVC
    19.44
)
list(FIND openmw_compiler_floors "${CMAKE_CXX_COMPILER_ID}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} is no compiler the tree is built with: GCC, Clang or MSVC")
endif()
math(EXPR at "${at} + 1")
list(GET openmw_compiler_floors ${at} floor)
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS floor)
    message(
        FATAL_ERROR
        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is older than ${floor}, the oldest the tree is built with"
    )
endif()

add_library(openmw-system INTERFACE)
if(MSVC)
    target_compile_options(
        openmw-system
        INTERFACE
            # sol3's bindings pass the default count of sections an object holds.
            /bigobj
            /utf-8
            /Zc:__cplusplus
    )
endif()
if(WIN32)
    target_compile_definitions(
        openmw-system
        INTERFACE
            NOMINMAX # name conflict with std::min, std::max
            WIN32_LEAN_AND_MEAN
            NOMB # name conflict with MWGui::MessageBox
            NOGDI # name conflict with osgAnimation::MorphGeometry::RELATIVE
            # Win32's UTF-16 API, which Qt 6's targets ask for: one setting for every target, so that each
            # can reuse another's precompiled headers.
            _UNICODE
            UNICODE
    )
    # One manifest for every program: per-monitor DPI awareness, long paths, no elevation.
    target_sources(
        openmw-system
        INTERFACE
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${PROJECT_SOURCE_DIR}/files/windows/openmw.manifest>
    )
endif()

add_library(openmw-checks INTERFACE)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(
        openmw-checks
        INTERFACE
            /W4
            /wd4100 # Unreferenced formal parameter (-Wunused-parameter)
            /wd4127 # Conditional expression is constant
            /wd4996 # Function was declared deprecated
            /wd5054 # Deprecated operations between enumerations of different types caused by Qt headers
            /wd4244 # Implicit narrowing between arithmetic types (-Wconversion, which GCC's -Wall -Wextra leave out)
            /wd4267 # Implicit narrowing of a size_t (the same)
            /w14855 # implicit capture of 'this' via '[=]' is deprecated in 'version'
    )
else()
    target_compile_options(
        openmw-checks
        INTERFACE
            -Wall
            -Wextra
            -Wundef
            -Wextra-semi
            -Wno-unused-parameter
            -pedantic
            -Wnon-virtual-dtor
            -Wshadow
            # Beyond upstream's. Rejected: `-Wfloat-equal`, for deliberate sentinels, and `-Wold-style-cast` and
            # `-Wuseless-cast`, which fire inside OpenSceneGraph's and MyGUI's headers.
            -Wnull-dereference
            -Wcast-qual
            $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>
            $<$<COMPILE_LANGUAGE:CXX>:-Wzero-as-null-pointer-constant>
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL Clang)
        target_compile_options(
            openmw-checks
            INTERFACE
                -Wno-potentially-evaluated-expression
                # GCC 15's `std::find_if` asks `#pragma GCC unroll 4`, which Clang 23 cannot always honour and
                # reports from inside the system header, where no other warning of its comes from.
                -Wno-pass-failed
        )
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL GNU)
        target_compile_options(
            openmw-checks
            INTERFACE
                # GCC's alone: Clang's check of that name takes every implicit promotion, not arithmetic's
                # only, and fired 614 times in a tree GCC's passes.
                -Wdouble-promotion
                -Wno-unused-but-set-parameter
                -Wduplicated-branches
                -Wduplicated-cond
                -Wlogical-op
                # GCC bug 105438, which GCC 15.2 still reports in sol's `stack_field.hpp`, a string literal's
                # bounds against another's length.
                -Wno-array-bounds
        )

        # `-Wmaybe-uninitialized` reports copies of a `std::optional` (GCC bug 80635), still on GCC 16.2
        # where inlining reaches across the set's headers: `MaterialResolver::read`'s `SurfaceDescription`.
        target_compile_options(openmw-checks INTERFACE -Wno-maybe-uninitialized)

        # Wrong before GCC 16: `-Wnull-dereference` reports OSG's inline `pushOntoNodePath`.
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
            target_compile_options(openmw-checks INTERFACE -Wno-null-dereference)
        endif()

        # Wrong on GCC 16: `-Wstringop-overflow` reports a byte vector's `resize(n, value)` at `-O3` as an
        # overflow no path reaches (GCC bugs 107852, 117983, 118521), and came back after a call site was
        # rewritten around it.
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
            target_compile_options(openmw-checks INTERFACE -Wno-stringop-overflow)
        endif()
    endif()
endif()

# Warnings are errors where a preset sets CMake's own `CMAKE_COMPILE_WARNING_AS_ERROR`: for the tree's
# targets, as `extern/` and the fetched projects turn it off for theirs, whose warnings are their own.

# Links `openmw-system` into every target a folder under `components/`, `apps/` or `extern/` defines, and
# `openmw-checks` into those under the first two, from `directory` down, but the interface and custom ones,
# which compile nothing; a target that compiles a third party's source says so with `OPENMW_CHECKS OFF`.
# Every folder is walked, as `components/rtxvulkan` is added from the root; one outside those three, a
# fetched project's, takes neither. Through the target's `LINK_LIBRARIES`, the property
# `target_link_libraries` fills, so a target linked with either of its signatures takes them alike.
function(openmw_link_tree directory)
    file(
        RELATIVE_PATH
        folder
        "${PROJECT_SOURCE_DIR}"
        "${directory}"
    )
    if(folder MATCHES "^(components|apps|extern)(/|$)")
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(type ${target} TYPE)
            if(type MATCHES "^(INTERFACE_LIBRARY|UTILITY)$")
                continue()
            endif()
            set_property(
                TARGET
                    ${target}
                APPEND
                PROPERTY
                    LINK_LIBRARIES
                        openmw-system
            )
            get_target_property(checks ${target} OPENMW_CHECKS)
            if(folder MATCHES "^(components|apps)(/|$)" AND NOT checks STREQUAL "OFF")
                set_property(
                    TARGET
                        ${target}
                    APPEND
                    PROPERTY
                        LINK_LIBRARIES
                            openmw-checks
                )
            endif()
        endforeach()
    endif()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        openmw_link_tree("${subdirectory}")
    endforeach()
endfunction()
