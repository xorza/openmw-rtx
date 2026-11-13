# What Linux's Clang adds to every compile and link of the set and the tree alike, beside `flags.cmake`'s code
# generation: read by the Linux triplet, which names this file among the inputs of every port's ABI hash, and by
# `cmake/Flags.cmake`. Windows' triplet reads neither it nor its hash, so a change here builds Linux's ports alone.
#
# - **GCC 15's installation** (`--gcc-install-dir`), whose libstdc++, start files and runtime Clang builds and links
#   with: it takes the newest GCC it finds otherwise, which a desk with GCC 16 beside 15 has, and AlmaLinux 8's
#   gcc-toolset is in no folder it searches. libstdc++'s C++20 half keeps no ABI across majors, and the set's
#   archives and the tree's objects share its templates. The GCC is `g++-15` where a system names it so, as Arch and
#   Ubuntu do, and `g++` otherwise, which the release base's `PATH` makes gcc-toolset-15's.
# - **No contraction** (`-ffp-contract=off`), so the arithmetic rounds as written, on Linux as under MSVC
#   (`vcpkg/flags.cmake`): Clang's default turns `a * b + c` into `llvm.fmuladd`, which x86-64-v3's fused
#   multiply-add rounds once. Before the tree had the instruction the optimiser already folded one fused where its
#   operands were constant, and `GroundReader::maskTransform` read one value in a test that folded it and another in
#   the reader that ran it.
set(openmw_gcc_major 15)
find_program(
    openmw_gcc
    NAMES
        g++-${openmw_gcc_major}
        g++
        NO_CACHE
    REQUIRED
)
execute_process(
    COMMAND
        "${openmw_gcc}" -dumpfullversion
    OUTPUT_VARIABLE openmw_gcc_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT openmw_gcc_version MATCHES "^${openmw_gcc_major}\\.")
    message(
        FATAL_ERROR
        "${openmw_gcc} is GCC ${openmw_gcc_version}, and the tree and the set build against GCC ${openmw_gcc_major}'s libstdc++: install g++-${openmw_gcc_major}"
    )
endif()
execute_process(
    COMMAND
        "${openmw_gcc}" -print-libgcc-file-name
    OUTPUT_VARIABLE openmw_libgcc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
get_filename_component(openmw_gcc_install_dir "${openmw_libgcc}" DIRECTORY)

# Every compile and link.
set(openmw_toolchain_linux "--gcc-install-dir=${openmw_gcc_install_dir}")
# Every compile.
set(openmw_codegen_linux "-ffp-contract=off")
