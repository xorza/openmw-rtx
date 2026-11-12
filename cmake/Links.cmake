# **How many programs link at once**: as many as the machine's memory holds of what one link of this build takes,
# never fewer than one, as LLVM bounds its own (`LLVM_PARALLEL_LINK_JOBS`). Compiles keep every core; it is at a
# build's end that its programs' links run together, and a 28 GB box with 16 threads linking every release program
# at once swapped until it answered nothing. Each figure is the largest program's link measured alone — the game,
# the harness, the tests or the CS — with a margin over it:
#
# - MSVC's `link.exe`: 7.2 GB private in release and 5.5 GB in debug, held at 8 GB, as the configuration is no
#   question a configure asks (`docs/rtx/build.md`); under link-time code generation, @LTCG@, at 12 GB.
# - lld: 3.7 GB resident under ThinLTO, 2.9 GB of it anonymous, held at 5 GB; 1.4 GB otherwise, sanitized or not,
#   0.36 GB of it anonymous, at 2 GB, where mold took 4.5 GB anonymous a sanitized link.
cmake_host_system_information(RESULT openmw_memory_mib QUERY TOTAL_PHYSICAL_MEMORY)
if(MSVC)
    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(openmw_link_mib 12288)
    else()
        set(openmw_link_mib 8192)
    endif()
elseif(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(openmw_link_mib 5120)
else()
    set(openmw_link_mib 2048)
endif()
math(EXPR openmw_links "${openmw_memory_mib} / ${openmw_link_mib}")
if(openmw_links LESS 1)
    set(openmw_links 1)
endif()
message(STATUS "Links at once: ${openmw_links}, of ${openmw_memory_mib} MiB at ${openmw_link_mib} MiB a link")
set(CMAKE_JOB_POOLS "link=${openmw_links}")
set(CMAKE_JOB_POOL_LINK link)
