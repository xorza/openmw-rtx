# Run at every build (`get-version` in `components/CMakeLists.txt`): the commit, the latest tag, the fork's
# release, the upstream commit it is based on and the copyright year into each of `FILES`, from its template in
# `TEMPLATES`, `<name>.in`, which the configure left with everything else filled in. `configure_file` writes a file
# only where its text moved. `SOURCE_DIR` is the clone.

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

# The fork's release as the package names it (`omw archive`): the nearest `rtx-v*` tag past its prefix, a commit
# after it as `26.11.0-12-gabc1234`, and the commit alone where no tag is reached.
execute_process(
    COMMAND
        ${GIT_EXECUTABLE} describe --tags --match rtx-v* --always
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE OPENMW_RTX_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
string(
    REGEX REPLACE "^rtx-v"
    ""
    OPENMW_RTX_VERSION
    "${OPENMW_RTX_VERSION}"
)

# Upstream's side of the newest merge of upstream: the workflow that merges it (`upstream.yml`) commits Git's own
# message for `git merge upstream/master`. Nothing in a clone shallower than that merge.
execute_process(
    COMMAND
        ${GIT_EXECUTABLE} log --merges -1 --format=%P "--grep=^Merge remote-tracking branch 'upstream/master'"
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE parents
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
string(
    REGEX MATCH "[0-9a-f]+$"
    OPENMW_UPSTREAM_COMMIT
    "${parents}"
)
if(NOT parents MATCHES " ")
    set(OPENMW_UPSTREAM_COMMIT "")
endif()

# The copyright year: `OPENMW_COPYRIGHT_YEAR` as the configure set it (`CMakeLists.txt`), or the commit's year where
# that is later.
execute_process(
    COMMAND
        ${GIT_EXECUTABLE} log -1 --format=%cI HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE commit_date
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT commit_date MATCHES "^([0-9][0-9][0-9][0-9])-")
    message(FATAL_ERROR "Git names no year in the commit's date '${commit_date}'")
endif()
if(CMAKE_MATCH_1 GREATER OPENMW_COPYRIGHT_YEAR)
    message(
        WARNING
        "Copyright year ${OPENMW_COPYRIGHT_YEAR} is older than the HEAD commit year ${CMAKE_MATCH_1}, using ${CMAKE_MATCH_1} instead"
    )
    set(OPENMW_COPYRIGHT_YEAR "${CMAKE_MATCH_1}")
endif()

foreach(file IN LISTS FILES)
    cmake_path(GET file FILENAME name)
    configure_file("${TEMPLATES}/${name}.in" "${file}" @ONLY)
endforeach()
