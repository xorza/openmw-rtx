# The crash catcher: Crashpad's client in the game, and its handler as the same executable started with
# `--crash-monitor`, one file to ship, as Chromium does it. Part of `openmw-components`, whose folders
# report through it and which it reads in turn.

target_sources(
    openmw-components
    PRIVATE
        crashanswering.cpp
        crashhangwatch.cpp
        crashmonitorarguments.cpp
        crashmonitorlog.cpp
        crashpackage.cpp
        crashpage.cpp
        crashsummary.cpp
    PUBLIC
        FILE_SET HEADERS
            FILES
                crash.hpp
                crashanswering.hpp
                crashhangwatch.hpp
                crashimagelinux.hpp
                crashinstall.hpp
                crashmonitorarguments.hpp
                crashmonitorlog.hpp
                crashpackage.hpp
                crashpadclientsystem.hpp
                crashpadmonitorsystem.hpp
                crashpadsummarysource.hpp
                crashpage.hpp
                crashsummary.hpp
                crashuncaught.hpp
)

# **The crash notes on their own**, a leaf the build's own tools link too: an error the renderer throws
# carries the thread's note (`Rtx::withNote`), and a tool that throws one links nothing else of this.
add_library(openmw-crash-notes STATIC crashnote.cpp)
target_sources(
    openmw-crash-notes
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                crashnote.hpp
)
target_link_libraries(openmw-crash-notes PUBLIC openmw-platform-io)
target_link_libraries(openmw-components PUBLIC openmw-crash-notes)

# The image the monitor reads, which Linux keeps for it and Windows has no need of.
if(LINUX)
    target_sources(openmw-components PRIVATE crashimagelinux.cpp)
else()
    target_sources(openmw-components PRIVATE crashimagenone.cpp)
endif()

target_sources(
    openmw-components
    PRIVATE
        crashpadclient.cpp
        crashpadmonitor.cpp
        crashpadsummarysource.cpp
)
if(WIN32)
    target_sources(
        openmw-components
        PRIVATE
            crashpadclientwin32.cpp
            crashpadmonitorwin32.cpp
    )
else()
    target_sources(
        openmw-components
        PRIVATE
            crashpadclientposix.cpp
            crashpadmonitorposix.cpp
    )
endif()

target_link_libraries(
    openmw-components
    PUBLIC
        crashpad_client
        crashpad_handler_lib
        crashpad_minidump
        crashpad_snapshot
        crashpad_tools
        crashpad_util
        mini_chromium
)
target_link_libraries(openmw-components PUBLIC $<$<CXX_COMPILER_ID:MSVC>:crashpad_getopt>)

# What Crashpad defines for its own directory and its headers read: the files that include them see
# what its library was built with. On the library's own folder, where source properties are read.
set_property(
    SOURCE
        crashpadclient.cpp
        crashpadmonitor.cpp
        crashpadsummarysource.cpp
        crashpadclientposix.cpp
        crashpadclientwin32.cpp
        crashpadmonitorposix.cpp
        crashpadmonitorwin32.cpp
        TARGET_DIRECTORY
        openmw-components
    APPEND
    PROPERTY
        COMPILE_DEFINITIONS
            CRASHPAD_FLOCK_ALWAYS_SUPPORTED=1
)

if(WIN32)
    # **Crashpad's WER module, beside the executables**, where the client lists it for the WER
    # service, which loads it at a fail-fast no handler inside the game sees. Named the fork's
    # own, so a stale entry the client removes is never another program's `crashpad_wer.dll`;
    # in the runtime folder, which the tree sets after `extern/` is added.
    set_target_properties(
        crashpad_wer
        PROPERTIES
            OUTPUT_NAME
                "openmw-wer"
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    add_dependencies(openmw-components crashpad_wer)
    # Installed here, at the executables' destination and without its import library: no
    # program links the module, so the runtime dependency set never sees it, and Crashpad's own
    # rule sits under `extern/`, whose `EXCLUDE_FROM_ALL` keeps its install script out of the
    # package.
    install(FILES $<TARGET_FILE:crashpad_wer> TYPE BIN)
    set_property(
        SOURCE
            crashpadclientwin32.cpp
            TARGET_DIRECTORY
            openmw-components
        APPEND
        PROPERTY
            COMPILE_DEFINITIONS
                "OPENMW_WER_MODULE=\"$<TARGET_FILE_NAME:crashpad_wer>\""
    )
endif()

if(LINUX)
    # **The note the monitor finds this module's `CrashpadInfo` by.** Crashpad's own reference
    # to it, in `GetCrashpadInfo`, is a store through a pointer *to* volatile, which GCC removes,
    # and a note nothing references stays in the archive: the annotations and every setting on
    # `CrashpadInfo`, the gathered heap among them, went unread. Asked for by name, it links.
    target_link_options(openmw-components INTERFACE "LINKER:--undefined=CRASHPAD_NOTE_REFERENCE")

    # **Every thread its own alternate signal stack**, so a stack overflow on a worker is a
    # report and not a second fault inside the handler. Crashpad's wrapper of `pthread_create`
    # gives it to each thread as it starts, drivers' and SDL's included; it has to be in the
    # executable itself to stand in front of libc's, so its object goes on every link line
    # that takes this library rather than into the archive.
    add_library(openmw-crashpad-threads OBJECT "${CRASHPAD_SOURCE_DIR}/client/pthread_create_linux.cc")
    target_link_libraries(openmw-crashpad-threads PRIVATE crashpad_client)
    # Crashpad's source, and so none of the tree's checks.
    set_target_properties(
        openmw-crashpad-threads
        PROPERTIES
            OPENMW_CHECKS
                OFF
    )
    target_compile_options(openmw-crashpad-threads PRIVATE -w)
    target_link_libraries(openmw-components PUBLIC $<TARGET_OBJECTS:openmw-crashpad-threads>)
endif()
