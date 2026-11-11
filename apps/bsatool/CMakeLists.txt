add_executable(bsatool)
target_sources(bsatool PRIVATE bsatool.cpp)
target_link_libraries(
    bsatool
    PRIVATE
        Boost::program_options
        openmw-components
)
