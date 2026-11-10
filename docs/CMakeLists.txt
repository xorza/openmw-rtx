# The API reference, by Doxygen: `doc` writes it under `docs/Doxygen`, `doc_pages` the pages'
# version under `OPENMW_DOXYGEN_PAGES_DIR`, `docs/Pages` unless a configure names another.

find_package(Sphinx REQUIRED)
find_package(Doxygen REQUIRED)

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
