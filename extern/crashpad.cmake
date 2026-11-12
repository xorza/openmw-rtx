# **Crashpad from Sentry's fork**, the one project that releases it with a CMake build: Crashpad
# itself is Chromium's and tags nothing. Its newest commit, three past what sentry-native 0.17.6
# pins, for two fixes of a stalled or external handler. Three archives,
# because GitHub's leaves the submodules out; each goes where the submodule would have been.
# Populated without being added, so that the two are in place before Crashpad's own
# CMakeLists reads them.
# The patch's digest on its command line, so an edit to it extracts Crashpad afresh and patches it again:
# FetchContent compares the command, not the script it runs.
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/crashpadpatch.cmake" crashpad_patch_digest)
FetchContent_Declare(
    crashpad
    URL
        https://github.com/getsentry/crashpad/archive/702c3d5a0d02c9b72a8b029d33bdfcd81dc75f65.zip
    URL_HASH
        SHA512=b616d5251bef188560a6a07bdc4b5ad603f6ef51a59a981d0f112adb52dd0086c01bf362f995c8ac7d0d57764591ef2a94992ce6889b31905c543eb6035c5630
    SOURCE_DIR fetched/crashpad
    SOURCE_SUBDIR added-below
    PATCH_COMMAND
        ${CMAKE_COMMAND} -DOPENMW_PATCH_DIGEST=${crashpad_patch_digest} -P ${CMAKE_CURRENT_LIST_DIR}/crashpadpatch.cmake
)
FetchContent_MakeAvailable(crashpad)

FetchContent_Declare(
    crashpad_mini_chromium
    URL
        https://github.com/getsentry/mini_chromium/archive/2f5c168fa462cf3836c4a7801b4f4955496d975d.zip
    URL_HASH
        SHA512=f970af0c12f0e2c23fc449cc944945ebbe6aabd81f6a0b72635b27efe47210c336d56b07486e3bc1e20e40b9b060e7ec4581db306e505abcb22f21c440e5b427
    SOURCE_DIR ${crashpad_SOURCE_DIR}/third_party/mini_chromium/mini_chromium
    SOURCE_SUBDIR not-a-project
)
FetchContent_Declare(
    crashpad_lss
    URL
        https://github.com/getsentry/chromium-linux-syscall-support/archive/9719c1e1e676814c456b55f5f070eabad6709d31.zip
    URL_HASH
        SHA512=f80c4e9ea6c88416c618965df90664932a31b0b7db80c5332edaf8982a4433074ccc6febd49f5086ce31d26e4b44527afbc90edc4ed45eab4ff4d1a8a2395869
    SOURCE_DIR ${crashpad_SOURCE_DIR}/third_party/lss/lss
    SOURCE_SUBDIR not-a-project
)
FetchContent_MakeAvailable(
    crashpad_mini_chromium
    crashpad_lss
)

# The zlib the tree links already, one copy in the process. Crashpad's own copy is the one it
# takes under MSVC alone.
set(CRASHPAD_ZLIB_SYSTEM ON CACHE BOOL "" FORCE)
set(CRASHPAD_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(CRASHPAD_ENABLE_INSTALL_DEV OFF CACHE BOOL "" FORCE)
# As a system's headers, because Chromium's are written against Chromium's warnings and not this
# tree's.
add_subdirectory(${crashpad_SOURCE_DIR} ${crashpad_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)

# Crashpad's Windows objects with the standard library's exceptions, as every other object of the program has them.
# Chromium builds a whole program with `_HAS_EXCEPTIONS=0`, under which MSVC's `std::exception` takes a virtual
# function more; beside the tree's objects, every standard exception had two vtables of different sizes, an ODR
# violation link-time code generation stops on (C4743 under Crashpad's `/WX`, LNK1257).
foreach(target crashpad_interface mini_chromium)
    foreach(property INTERFACE_COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
        get_target_property(definitions ${target} ${property})
        if(definitions)
            list(FILTER definitions EXCLUDE REGEX "^_HAS_EXCEPTIONS(=|$)")
            set_target_properties(
                ${target}
                PROPERTIES
                    ${property}
                        "${definitions}"
            )
        endif()
    endforeach()
endforeach()

# `capture_context_linux.S` states nothing of the stack, and a linker that meets an object
# without the note makes the whole executable's stack executable, as Ubuntu 24.04's does.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(crashpad_util PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wa,--noexecstack>)
endif()

# For `components/crashcatcher/CMakeLists.txt`, which builds one of its files into every executable.
set(CRASHPAD_SOURCE_DIR ${crashpad_SOURCE_DIR})
