add_executable(openmw_settings_access_benchmark access.cpp)
target_link_libraries(
    openmw_settings_access_benchmark
    PRIVATE
        benchmark::benchmark
        openmw-components
)
target_compile_definitions(openmw_settings_access_benchmark PRIVATE OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")
