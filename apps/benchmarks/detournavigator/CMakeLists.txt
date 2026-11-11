add_executable(openmw_detournavigator_navmeshtilescache_benchmark navmeshtilescache.cpp)
target_link_libraries(
    openmw_detournavigator_navmeshtilescache_benchmark
    PRIVATE
        benchmark::benchmark
        openmw-components
)
