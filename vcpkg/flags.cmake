# The code generation every object of the game is compiled with, the vcpkg set's and the tree's alike: read by
# both triplets, which name this file among the inputs of every port's ABI hash, and by `cmake/Flags.cmake`.
# A change here builds a new set.
#
# - Line tables (MSVC: vcpkg's and the tree's `/Z7`) and frame pointers, so a profile walks the stack and a crash
#   names a line in any library.
# - x86-64-v2 (SSE4.2, POPCNT; MSVC's `/arch:SSE4.2`), which every CPU beside an RTX 20 or an RDNA 2 card has;
#   no fused multiply-add, which only v3 brings, so the arithmetic rounds as written.
# - Each function and global in a section of its own, which the link drops where nothing reaches it, inside the
#   set's archives too (GCC's sections and `--gc-sections`; MSVC's `/Gy`, which `/O2` implies, `/Gw` and
#   `/OPT:REF`). `/Zc:inline` drops an unreferenced inline function before the object is written.
# - The stack protector GCC's distributions and MSVC (`/GS`) default to, against the untrusted files the game
#   reads; the release base's GCC has it off unless asked.
# - No semantic interposition: vcpkg compiles every Linux port with `-fPIC`, under which GCC would otherwise
#   inline no exported function of a library into another, for a program that links all of them statically.
set(openmw_codegen_gnu
    "-g1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -march=x86-64-v2 -ffunction-sections -fdata-sections -fstack-protector-strong -fno-semantic-interposition"
)
set(openmw_codegen_msvc "/Oy- /arch:SSE4.2 /Gw /Zc:inline")
