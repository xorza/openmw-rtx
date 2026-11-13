add_executable(openmw_rtx_chain_encode_benchmark chainencode.cpp)
target_link_libraries(
    openmw_rtx_chain_encode_benchmark
    PRIVATE
        benchmark::benchmark
        openmw-rtx-preprocess
)
