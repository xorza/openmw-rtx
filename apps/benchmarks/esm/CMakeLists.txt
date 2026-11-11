add_executable(openmw_esm_refid_benchmark benchrefid.cpp)
target_link_libraries(
    openmw_esm_refid_benchmark
    PRIVATE
        benchmark::benchmark
        openmw-components
)
