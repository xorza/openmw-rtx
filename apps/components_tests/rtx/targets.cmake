if (OPENMW_RTX)
    # Vulkan itself as well, because these tests reach into the backend's own headers, which a host
    # does not: `components/rtx/build.cmake` says where it is found and why.
    target_link_libraries(components-tests openmw-rtx
                          openmw-rtx-vulkan openmw-rtx-mygui openmw-rtxtool-lib openmw-rtx-spirv Vulkan::Vulkan)

    # The SPIR-V headers, for the tests that write a module word by word to hand the pinning.
    target_include_directories(components-tests SYSTEM PRIVATE "${OPENMW_SPIRV_HEADERS}")

    # Where the build wrote the shaders, for the tests that read a module or stage the resources
    # beside them. Told to the two test binaries and to nothing else: a path into the build tree is
    # no fact about the backend.
    target_compile_definitions(components-tests PRIVATE OPENMW_RTX_SHADER_DIR="${RTX_SPIRV_DIR}")

    # Per file rather than per target: this binary holds upstream's tests as well as this fork's, and
    # only the latter are ours to keep warning-free.
    openmw_rtx_sources(${RTX_TEST_FILES} ${RTX_TEST_SUPPORT} ${RTX_TEST_FILES_EITHER} ERRORS_ONLY ${RTX_TEST_FILES_UPSTREAM})

    # The tests that open a device, as a binary of their own: `rtx/support/device/harness.cpp`
    # refuses to start without one, so a machine with no driver fails this run instead of passing an
    # empty one. The same `main.cpp`, because what it sets up — the settings' defaults — is what both
    # binaries read.
    openmw_add_executable(rtx-gpu-tests main.cpp rtx/support/allocations.cpp
                          ${RTX_GPU_TEST_FILES} ${RTX_GPU_TEST_SUPPORT} ${RTX_TEST_SUPPORT})
    target_link_libraries(rtx-gpu-tests
        GTest::GTest
        GMock::GMock
        components
        openmw-rtx openmw-rtx-vulkan openmw-rtxtool-lib Vulkan::Vulkan
    )
    openmw_rtx_sources(${RTX_GPU_TEST_FILES} ${RTX_GPU_TEST_SUPPORT})
    target_compile_definitions(rtx-gpu-tests
        PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
                OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}"
                OPENMW_RTX_SHADER_DIR="${RTX_SPIRV_DIR}")
    if (UNIX AND NOT APPLE)
        target_link_libraries(rtx-gpu-tests ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()
