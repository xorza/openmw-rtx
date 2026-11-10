add_executable(openmw_detournavigator_navmeshtilescache_benchmark navmeshtilescache.cpp)
target_link_libraries(openmw_detournavigator_navmeshtilescache_benchmark benchmark::benchmark openmw-components)

if (UNIX)
    target_link_libraries(openmw_detournavigator_navmeshtilescache_benchmark ${CMAKE_THREAD_LIBS_INIT})
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw_detournavigator_navmeshtilescache_benchmark REUSE_FROM openmw-components)
endif()

if (WIN32)
    target_sources(openmw_detournavigator_navmeshtilescache_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()
