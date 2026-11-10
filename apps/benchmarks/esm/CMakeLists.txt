add_executable(openmw_esm_refid_benchmark benchrefid.cpp)
target_link_libraries(openmw_esm_refid_benchmark benchmark::benchmark openmw-components)

if (UNIX)
    target_link_libraries(openmw_esm_refid_benchmark ${CMAKE_THREAD_LIBS_INIT})
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw_esm_refid_benchmark REUSE_FROM openmw-components)
endif()

if (WIN32)
    target_sources(openmw_esm_refid_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()
