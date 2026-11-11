add_executable(openmw-bulletobjecttool)
target_sources(openmw-bulletobjecttool PRIVATE main.cpp)
target_link_libraries(
    openmw-bulletobjecttool
    PRIVATE
        Boost::program_options
        openmw-components
)
