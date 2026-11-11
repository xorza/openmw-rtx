# The build's resources, into the folder the programs run from: copied, configured, or packed with base64
# (`cmake/base64.cmake`), each at configure. Every preset is one configuration, so one folder.
function(copy_resource_file source destination_root relative)
    configure_file("${source}" "${destination_root}/${relative}" COPYONLY)
endfunction()

function(configure_resource_file source destination_root relative)
    configure_file("${source}" "${destination_root}/${relative}")
endfunction()

function(pack_resource_file source destination_root relative)
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} "-DINPUT_FILE=${source}" "-DOUTPUT_FILE=${destination_root}/${relative}" -P
            "${PROJECT_SOURCE_DIR}/cmake/base64.cmake"
        COMMAND_ERROR_IS_FATAL ANY
    )
    set_property(
        DIRECTORY
        APPEND
        PROPERTY
            CMAKE_CONFIGURE_DEPENDS
                "${source}"
    )
endfunction()

function(copy_all_resource_files source_dir destination_root destination_relative files)
    foreach(file IN LISTS files)
        copy_resource_file("${source_dir}/${file}" "${destination_root}" "${destination_relative}/${file}")
    endforeach()
endfunction()

# The windowing platforms a Qt program opens on, which the static Qt links into it: Qt's default, X11's on
# Linux and Windows' own, and on Linux Wayland's beside it, so a session with no XWayland opens it too.
function(openmw_qt_platforms target)
    if(LINUX)
        qt_import_plugins(${target} INCLUDE Qt6::QWaylandIntegrationPlugin)
    endif()
endfunction()
