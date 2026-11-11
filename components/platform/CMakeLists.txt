# What systems answer differently, one header each and a file a system: `openmw-platform-io` on its own,
# and the rest part of `openmw-components`.

# **A file read and written whole, and the process it is written by**, a library of its own: the build's own
# tools (`openmw_rtx_table`) write through it, and they link no more of the components than this.
add_library(openmw-platform-io STATIC)
target_sources(
    openmw-platform-io
    PRIVATE
        folder.cpp
        linuxtext.cpp
        wholefile.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                folder.hpp
                linuxtext.hpp
                process.hpp
                wholefile.hpp
                wholefilesystem.hpp
)
if(WIN32)
    target_sources(
        openmw-platform-io
        PRIVATE
            processwin32.cpp
            wholefilewin32.cpp
    )
elseif(LINUX)
    target_sources(
        openmw-platform-io
        PRIVATE
            kernelfilelinux.cpp
            processlinux.cpp
            wholefileposix.cpp
        PUBLIC
            FILE_SET HEADERS
                FILES
                    kernelfilelinux.hpp
    )
endif()

target_sources(
    openmw-components
    PRIVATE
        platform.cpp
        thread.cpp
    PUBLIC
        FILE_SET HEADERS
            FILES
                appendfile.hpp
                fifo.hpp
                file.hpp
                gpu.hpp
                heap.hpp
                library.hpp
                localtime.hpp
                memory.hpp
                platform.hpp
                sharedmemory.hpp
                thread.hpp
                uniquehold.hpp
                userhome.hpp
)
if(WIN32)
    target_sources(
        openmw-components
        PRIVATE
            appendfilewin32.cpp
            fifowin32.cpp
            filewin32.cpp
            gpu.cpp
            gpuwin32.cpp
            heapwin32.cpp
            librarywin32.cpp
            localtimewin32.cpp
            memorywin32.cpp
            sharedmemorywin32.cpp
            threadwin32.cpp
            userhomewin32.cpp
    )
elseif(LINUX)
    target_sources(
        openmw-components
        PRIVATE
            appendfileposix.cpp
            fifoposix.cpp
            fileposix.cpp
            gpu.cpp
            gpulinux.cpp
            heapposix.cpp
            libraryposix.cpp
            localtimeposix.cpp
            memorylinux.cpp
            sharedmemoryposix.cpp
            threadlinux.cpp
            userhomeposix.cpp
    )
else()
    message(
        FATAL_ERROR
        "The platform is written for Linux and Windows alone: "
        "${CMAKE_SYSTEM_NAME} needs files of its own in components/platform"
    )
endif()
