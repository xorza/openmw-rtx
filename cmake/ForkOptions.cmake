# The options this fork adds: the ray tracer and the crash catcher.

# The ray tracer is Vulkan on NVIDIA hardware, which Windows and Linux have and macOS does not.
if (WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID))
    set(OPENMW_RTX_DEFAULT ON)
else()
    set(OPENMW_RTX_DEFAULT OFF)
endif()
option(OPENMW_RTX               "Build the experimental ray tracing renderer" ${OPENMW_RTX_DEFAULT})
option(OPENMW_RTX_DLSS          "Upscale and denoise with DLSS Ray Reconstruction" ON)

# Crashpad captures a crash out of process on every system it supports. FreeBSD, which it does not
# support, has no crash catcher, and says so in its log.
if (WIN32 OR APPLE OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID))
    set(OPENMW_CRASHPAD_DEFAULT ON)
else()
    set(OPENMW_CRASHPAD_DEFAULT OFF)
endif()
option(OPENMW_CRASHPAD          "Capture crashes with Crashpad" ${OPENMW_CRASHPAD_DEFAULT})
