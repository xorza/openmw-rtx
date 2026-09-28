# The ray tracer's own answers, in the same shape: what its perf fifo and the libraries it opens do
# that the systems spell differently. Two systems, because those are the two it runs on.
if (OPENMW_RTX)
    add_component_dir(platform
        fifo library
        )

    if (WIN32)
        add_component_dir(platform
            fifowin32 librarywin32
            )
    elseif (UNIX)
        add_component_dir(platform
            fifoposix libraryposix
            )
    else ()
        message(FATAL_ERROR "The ray tracer has no platform layer for ${CMAKE_SYSTEM_NAME}")
    endif()
endif()
