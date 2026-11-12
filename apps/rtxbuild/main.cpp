#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

#include "verbs.hpp"

// `openmw-rtx-build <verb> <arguments>`: what the build runs to make the ray tracer's sources, one program for
// every step, as `llvm-tblgen` is one generator with a backend a table. It runs in the build alone and no install
// carries it.

namespace
{
    struct Verb
    {
        std::string_view mName;
        std::string_view mArguments;
        std::size_t mLeast;
        std::size_t mMost;
        void (*mRun)(RtxBuild::Arguments);
    };

    constexpr std::size_t sUnbounded = std::numeric_limits<std::size_t>::max();

    constexpr std::array sVerbs{
        Verb{ "guard", "<module.spv>", 1, 1, &RtxBuild::guard },
        Verb{ "pin", "<module.spv>", 1, 1, &RtxBuild::pin },
        Verb{ "embed", "<source.cpp> <module.spv>...", 2, sUnbounded, &RtxBuild::embed },
        Verb{ "digest", "< disassembly", 0, 0, &RtxBuild::digest },
        Verb{ "blue-noise", "<table.cpp>", 1, 1, &RtxBuild::blueNoise },
        Verb{ "fog-noise", "<table.cpp>", 1, 1, &RtxBuild::fogNoise },
        Verb{ "specular-albedo", "<table.cpp>", 1, 1, &RtxBuild::specularAlbedo },
    };

    int usage()
    {
        std::cerr << "usage: openmw-rtx-build <verb> <arguments>, where <verb> is one of\n";
        for (const Verb& verb : sVerbs)
            std::cerr << "  " << verb.mName << ' ' << verb.mArguments << '\n';
        return 2;
    }
}

int main(int argc, char* argv[])
{
    const std::span<char* const> line(argv, static_cast<std::size_t>(argc));
    if (line.size() < 2)
        return usage();

    const std::string_view name(line[1]);
    const auto found = std::ranges::find(sVerbs, name, &Verb::mName);
    const RtxBuild::Arguments arguments = line.subspan(2);
    if (found == sVerbs.end() || arguments.size() < found->mLeast || arguments.size() > found->mMost)
        return usage();

    try
    {
        found->mRun(arguments);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "openmw-rtx-build " << name << ": " << error.what() << '\n';
        return 1;
    }
}
