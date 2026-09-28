if (OPENMW_CRASHPAD)
    # Crashpad's client, and its handler as a library, because the monitor is this same
    # executable started with `--crash-monitor`: one file to ship, as Chromium does it.
    target_link_libraries(components
        crashpad_client
        crashpad_handler_lib
        crashpad_minidump
        crashpad_snapshot
        crashpad_tools
        crashpad_util
        mini_chromium
    )
    if (MSVC)
        target_link_libraries(components crashpad_getopt)
    endif()

    # What Crashpad defines for its own directory and its headers read: the files that include them
    # see what its library was built with.
    set_property(SOURCE crashcatcher/crashpadclient.cpp crashcatcher/crashpadmonitor.cpp
        crashcatcher/crashpadclientposix.cpp crashcatcher/crashpadclientwin32.cpp
        crashcatcher/crashpadmonitorposix.cpp crashcatcher/crashpadmonitorwin32.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS CRASHPAD_FLOCK_ALWAYS_SUPPORTED=1)

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # **The note the monitor finds this module's `CrashpadInfo` by.** Crashpad's own reference
        # to it, in `GetCrashpadInfo`, is a store through a pointer *to* volatile, which GCC removes,
        # and a note nothing references stays in the archive: the annotations and every setting on
        # `CrashpadInfo`, the gathered heap among them, went unread. Asked for by name, it links.
        target_link_options(components INTERFACE "LINKER:--undefined=CRASHPAD_NOTE_REFERENCE")

        # **Every thread its own alternate signal stack**, so a stack overflow on a worker is a
        # report and not a second fault inside the handler. Crashpad's wrapper of `pthread_create`
        # gives it to each thread as it starts, drivers' and SDL's included; it has to be in the
        # executable itself to stand in front of libc's, so its object goes on every link line
        # that takes this library rather than into the archive.
        add_library(openmw_crashpad_threads OBJECT ${CRASHPAD_SOURCE_DIR}/client/pthread_create_linux.cc)
        target_link_libraries(openmw_crashpad_threads PRIVATE crashpad_client)
        target_compile_options(openmw_crashpad_threads PRIVATE -w)
        target_link_libraries(components $<TARGET_OBJECTS:openmw_crashpad_threads>)
    endif()
endif()
