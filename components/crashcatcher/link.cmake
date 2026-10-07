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

    if (WIN32)
        # **Crashpad's WER module, beside the executables**, where the client lists it for the WER
        # service, which loads it at a fail-fast no handler inside the game sees. Named the fork's
        # own, so a stale entry the client removes is never another program's `crashpad_wer.dll`;
        # in the runtime folder, which the tree sets after `extern/` is added.
        set_target_properties(crashpad_wer PROPERTIES OUTPUT_NAME "openmw-wer"
            RUNTIME_OUTPUT_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}")
        add_dependencies(components crashpad_wer)
        # Installed here, at the executables' destination and without its import library: no
        # program links the module, so the runtime dependency set never sees it, and Crashpad's own
        # rule sits under `extern/`, whose `EXCLUDE_FROM_ALL` keeps its install script out of the
        # package.
        install(FILES $<TARGET_FILE:crashpad_wer> TYPE BIN)
        set_property(SOURCE crashcatcher/crashpadclientwin32.cpp
            APPEND PROPERTY COMPILE_DEFINITIONS "OPENMW_WER_MODULE=\"$<TARGET_FILE_NAME:crashpad_wer>\"")
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
