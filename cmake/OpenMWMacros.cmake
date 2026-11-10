function (generate_deploy_script_for_qt_app target)
    if (UNIX AND OPENMW_DEPLOY_RUNTIME_DEPENDENCIES)
        if (Qt6Core_VERSION VERSION_LESS 6.10)
            message(FATAL_ERROR "Deployment of Qt on Linux requires at least 6.10 (for the INCLUDE_PLUGINS argument)")
        endif ()
        # don't bother with this on Windows yet as windeployqt includes a bunch of DLLs we don't use
        cmake_path(NORMAL_PATH QT_DIR OUTPUT_VARIABLE QT_BASE_DIR)
        string(REGEX REPLACE "(.+)/lib/cmake/Qt[0-9]+" "\\1" QT_BASE_DIR "${QT_BASE_DIR}")
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

macro (get_generator_is_multi_config VALUE)
    # TODO: make git version getter less dumb so everywhere that calls this macro can just call the function this calls instead
    if (DEFINED generator_is_multi_config_var)
        set(${VALUE} ${generator_is_multi_config_var})
    else (DEFINED generator_is_multi_config_var)
        get_cmake_property(${VALUE} GENERATOR_IS_MULTI_CONFIG)
    endif (DEFINED generator_is_multi_config_var)
endmacro (get_generator_is_multi_config)

macro (copy_resource_file source_path destination_dir_base dest_path_relative)
    get_generator_is_multi_config(multi_config)
    if (multi_config)
        foreach(cfgtype ${CMAKE_CONFIGURATION_TYPES})
            configure_file(${source_path} "${destination_dir_base}/${cfgtype}/${dest_path_relative}" COPYONLY)
        endforeach(cfgtype)
    else (multi_config)
        configure_file(${source_path} "${destination_dir_base}/${dest_path_relative}" COPYONLY)
    endif (multi_config)
endmacro (copy_resource_file)

macro (configure_resource_file source_path destination_dir_base dest_path_relative)
    get_generator_is_multi_config(multi_config)
    if (multi_config)
        foreach(cfgtype ${CMAKE_CONFIGURATION_TYPES})
            configure_file(${source_path} "${destination_dir_base}/${cfgtype}/${dest_path_relative}")
        endforeach(cfgtype)
    else (multi_config)
        configure_file(${source_path} "${destination_dir_base}/${dest_path_relative}")
    endif (multi_config)
endmacro (configure_resource_file)

macro (pack_resource_file source_path destination_dir_base dest_path_relative)
    get_generator_is_multi_config(multi_config)
    if (multi_config)
        foreach(cfgtype ${CMAKE_CONFIGURATION_TYPES})
            execute_process(COMMAND ${CMAKE_COMMAND} "-DINPUT_FILE=${source_path}" "-DOUTPUT_FILE=${destination_dir_base}/${cfgtype}/${dest_path_relative}" -P "${CMAKE_SOURCE_DIR}/cmake/base64.cmake")
        endforeach(cfgtype)
    else (multi_config)
        execute_process(COMMAND ${CMAKE_COMMAND} "-DINPUT_FILE=${source_path}" "-DOUTPUT_FILE=${destination_dir_base}/${dest_path_relative}" -P "${CMAKE_SOURCE_DIR}/cmake/base64.cmake")
    endif (multi_config)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_path}")
endmacro (pack_resource_file)

macro (copy_all_resource_files source_dir destination_dir_base destination_dir_relative files)
    foreach (f ${files})
        get_filename_component(filename ${f} NAME)
        copy_resource_file("${source_dir}/${f}" "${destination_dir_base}" "${destination_dir_relative}/${f}")
    endforeach (f)
endmacro (copy_all_resource_files)
