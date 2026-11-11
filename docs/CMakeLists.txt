# The API reference, by Doxygen: `doc` writes it under `docs/Doxygen`, `doc_pages` the pages'
# version under `OPENMW_DOXYGEN_PAGES_DIR`, `docs/Pages` unless a configure names another.

# Doxygen is a desk's own, which no build needs: without it the two targets say so when asked for, and the
# configure is the same.
find_package(Doxygen QUIET)
if(NOT DOXYGEN_FOUND)
    foreach(target doc doc_pages)
        add_custom_target(
            ${target}
            COMMAND
                ${CMAKE_COMMAND} -E echo "${target}: Doxygen is not installed"
            COMMAND
                ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endforeach()
    return()
endif()

configure_file(mainpage.hpp.cmake mainpage.hpp)
if(NOT DEFINED OPENMW_DOXYGEN_PAGES_DIR)
    set(OPENMW_DOXYGEN_PAGES_DIR "${PROJECT_BINARY_DIR}/docs/Pages")
endif()
configure_file(Doxyfile.cmake Doxyfile @ONLY)
configure_file(DoxyfilePages.cmake DoxyfilePages @ONLY)
add_custom_target(
    doc
    ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    COMMENT "Generating Doxygen documentation at ${PROJECT_BINARY_DIR}/docs/Doxygen"
    VERBATIM
)
add_custom_target(
    doc_pages
    ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/DoxyfilePages"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    COMMENT "Generating documentation for the github-pages at ${OPENMW_DOXYGEN_PAGES_DIR}"
    VERBATIM
)
