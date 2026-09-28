# `Settings::sRayTracingBuilt` and `Rtx::sUpscalerBuilt` read these: facts about the binary, and
# every program in this tree links this library, the settings menu that reads `upscale.hpp` in a
# build without the ray tracer included. Defined in every build, as 0 or 1, so each constant needs
# no `#ifdef`.
target_compile_definitions(components PUBLIC OPENMW_RTX=$<BOOL:${OPENMW_RTX}>
                                             OPENMW_RTX_DLSS=$<BOOL:$<AND:$<BOOL:${OPENMW_RTX}>,$<BOOL:${OPENMW_RTX_DLSS}>>>)

if (OPENMW_RTX)
    # What `platform/libraryposix.cpp` opens a shared library with, on the C libraries that keep
    # it out of libc. Empty where libc has it.
    target_link_libraries(components ${CMAKE_DL_LIBS})

    # This fork's own files inside upstream's library take the fork's flags, for the reason
    # `components/rtx/build.cmake` gives; the upstream files the fork edits keep upstream's. Every
    # system's file is named, and a name no system builds sets a property nothing reads.
    openmw_rtx_sources(
        platform/fifoposix.cpp platform/fifowin32.cpp platform/libraryposix.cpp platform/librarywin32.cpp
        platform/memoryposix.cpp platform/memorywin32.cpp platform/processposix.cpp platform/processwin32.cpp
        platform/sharedmemoryposix.cpp platform/sharedmemorywin32.cpp
        sceneutil/paintedtexture.cpp sky/sundisc.cpp sky/timeofday.cpp
        )
endif()
