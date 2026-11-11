add_subdirectory(shaders)
add_subdirectory(data)
add_subdirectory(data-mw)
add_subdirectory(lua_api)
add_subdirectory(lang)

# The settings' defaults, the configuration a build runs with, and what the programs read beside them,
# into the build where the programs run from. The two defaults packed with base64 (`cmake/base64.cmake`), as the
# programs read them, at configure as the rest are copied.
set(packed_sources
    settings-default.cfg
    openmw-cs.cfg
)
set(packed_files
    defaults.bin
    defaults-cs.bin
)
foreach(packed IN ZIP_LISTS packed_sources packed_files)
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/${packed_0}"
            "-DOUTPUT_FILE=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${packed_1}" -P "${PROJECT_SOURCE_DIR}/cmake/base64.cmake"
        COMMAND_ERROR_IS_FATAL ANY
    )
    set_property(
        DIRECTORY
        APPEND
        PROPERTY
            CMAKE_CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/${packed_0}"
    )
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/openmw.cfg.local" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/openmw.cfg")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/gamecontrollerdb.txt"
)
# Copied and not configured: `configure_file` reads to the first null character, which this file holds.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/opencs/defaultfilters"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/defaultfilters"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/launcher/images/openmw.png"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/openmw.png"
    COPYONLY
)

if(NOT WIN32)
    configure_file(org.openmw.launcher.desktop "${PROJECT_BINARY_DIR}/org.openmw.launcher.desktop")
    configure_file(org.openmw.cs.desktop "${PROJECT_BINARY_DIR}/org.openmw.cs.desktop")
endif()
