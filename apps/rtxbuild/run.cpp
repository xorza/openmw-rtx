#include "run.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <limits>
#include <ostream>
#include <span>
#include <string_view>

#include "verbs.hpp"

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

    int usage(std::ostream& errors)
    {
        errors << "usage: openmw-rtx-build <verb> <arguments>, where <verb> is one of\n";
        for (const Verb& verb : sVerbs)
            errors << "  " << verb.mName << ' ' << verb.mArguments << '\n';
        return 2;
    }
}

namespace RtxBuild
{
    int run(const std::span<char* const> line, std::ostream& errors)
    {
        if (line.size() < 2)
            return usage(errors);

        const std::string_view name(line[1]);
        const auto found = std::ranges::find(sVerbs, name, &Verb::mName);
        if (found == sVerbs.end())
        {
            errors << "openmw-rtx-build: no verb is called " << name << '\n';
            return usage(errors);
        }

        const Arguments arguments = line.subspan(2);
        if (arguments.size() < found->mLeast || arguments.size() > found->mMost)
        {
            errors << "openmw-rtx-build " << name << ": takes " << found->mArguments << '\n';
            return 2;
        }

        try
        {
            found->mRun(arguments);
            return 0;
        }
        catch (const std::exception& error)
        {
            errors << "openmw-rtx-build " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
}
