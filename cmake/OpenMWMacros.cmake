function(generate_deploy_script_for_qt_app target)
    if(UNIX AND OPENMW_DEPLOY_RUNTIME_DEPENDENCIES)
        if(Qt6Core_VERSION VERSION_LESS 6.10)
            message(FATAL_ERROR "Deployment of Qt on Linux requires at least 6.10 (for the INCLUDE_PLUGINS argument)")
        endif()
        # don't bother with this on Windows yet as windeployqt includes a bunch of DLLs we don't use
        cmake_path(NORMAL_PATH QT_DIR OUTPUT_VARIABLE QT_BASE_DIR)
        string(
            REGEX REPLACE "(.+)/lib/cmake/Qt[0-9]+"
            "\\1"
            QT_BASE_DIR
            "${QT_BASE_DIR}"
        )
        escape_regex_control_characters(QT_BASE_REGEX "${QT_BASE_DIR}")
        qt_generate_deploy_app_script(TARGET ${target}
            OUTPUT_SCRIPT deploy_script
            NO_TRANSLATIONS
            NO_COMPILER_RUNTIME
            INCLUDE_PLUGINS wayland
            POST_INCLUDE_REGEXES
                "${QT_BASE_REGEX}"
            POST_EXCLUDE_REGEXES
                ".*"
        )
        install(SCRIPT ${deploy_script})
    endif()
endfunction()

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
