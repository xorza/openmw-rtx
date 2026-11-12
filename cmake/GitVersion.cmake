# Run at every build (`get-version` in `components/CMakeLists.txt`): the commit and the latest tag
# into each of `FILES`, from its template in `TEMPLATES`, `<name>.in`, which the configure left with everything
# else filled in. `configure_file` writes a file only where its text moved. `SOURCE_DIR` is the clone.

execute_process(
    COMMAND
        ${GIT_EXECUTABLE} rev-parse HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE OPENMW_VERSION_COMMITHASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
# A clone without tags, as a shallow checkout is, names none.
execute_process(
    COMMAND
        ${GIT_EXECUTABLE} rev-list --tags --max-count=1
    WORKING_DIRECTORY ${SOURCE_DIR}
    RESULT_VARIABLE failed
    OUTPUT_VARIABLE OPENMW_VERSION_TAGHASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT failed EQUAL 0)
    set(OPENMW_VERSION_TAGHASH "")
endif()

foreach(file IN LISTS FILES)
    cmake_path(GET file FILENAME name)
    configure_file("${TEMPLATES}/${name}.in" "${file}" @ONLY)
endforeach()
