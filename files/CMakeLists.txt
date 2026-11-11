add_subdirectory(shaders)
add_subdirectory(data)
add_subdirectory(data-mw)
add_subdirectory(lua_api)
add_subdirectory(lang)

# The settings' defaults, the configuration a build runs with, and what the programs read beside them,
# into the build where the programs run from.
pack_resource_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/settings-default.cfg"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    "defaults.bin"
)
pack_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/openmw-cs.cfg" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "defaults-cs.bin")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/openmw.appdata.xml" "${PROJECT_BINARY_DIR}/openmw.appdata.xml")
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
