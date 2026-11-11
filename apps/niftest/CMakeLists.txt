add_executable(niftest)
target_sources(niftest PRIVATE niftest.cpp)
target_link_libraries(niftest PRIVATE openmw-components)

install(
    TARGETS
        niftest
        ${runtime_dependency_arguments}
)
