add_subdirectory(shaders)
add_subdirectory(data)
add_subdirectory(data-mw)
add_subdirectory(lua_api)
if(USE_QT)
    add_subdirectory(lang)
endif()

# The settings' defaults, the configuration a build runs with, and what the programs read beside them,
# into the build where the programs run from.
pack_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/settings-default.cfg" "${PROJECT_BINARY_DIR}" "defaults.bin")
pack_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/openmw-cs.cfg" "${PROJECT_BINARY_DIR}" "defaults-cs.bin")
configure_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/openmw.appdata.xml" "${PROJECT_BINARY_DIR}" "openmw.appdata.xml")
configure_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/openmw.cfg.local" "${PROJECT_BINARY_DIR}" "openmw.cfg")
configure_resource_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt"
    "${PROJECT_BINARY_DIR}"
    "gamecontrollerdb.txt"
)
# Copied and not configured: `configure_file` reads to the first null character, which this file holds.
copy_resource_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/opencs/defaultfilters"
    "${PROJECT_BINARY_DIR}"
    "resources/defaultfilters"
)
copy_resource_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/launcher/images/openmw.png"
    "${OPENMW_RESOURCES_ROOT}"
    "resources/openmw.png"
)

if(NOT WIN32)
    configure_file(org.openmw.launcher.desktop "${PROJECT_BINARY_DIR}/org.openmw.launcher.desktop")
    configure_file(org.openmw.cs.desktop "${PROJECT_BINARY_DIR}/org.openmw.cs.desktop")
endif()
