add_executable(openmw_detournavigator_navmeshtilescache_benchmark navmeshtilescache.cpp)
if (WIN32)
    target_sources(openmw_detournavigator_navmeshtilescache_benchmark PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif ()
target_link_libraries(openmw_detournavigator_navmeshtilescache_benchmark PRIVATE benchmark::benchmark openmw-components)

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw_detournavigator_navmeshtilescache_benchmark REUSE_FROM openmw-components)
endif ()
