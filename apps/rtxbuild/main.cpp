#include <cstddef>
#include <iostream>
#include <span>

#include "run.hpp"

// `openmw-rtx-build <verb> <arguments>`: what the build runs to make the ray tracer's sources, one program for
// every step, as `llvm-tblgen` is one generator with a backend a table. It runs in the build alone and no install
// carries it.

int main(int argc, char* argv[])
{
    return RtxBuild::run(std::span<char* const>(argv, static_cast<std::size_t>(argc)), std::cerr);
}
