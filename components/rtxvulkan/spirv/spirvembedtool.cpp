#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <components/platform/wholefile.hpp>
#include <components/rtx/renderer/shaderset.hpp>

#include "spirvfile.hpp"

// `openmw-rtx-spirv-embed <source.cpp> <module.spv>...`: the C++ source that compiles the modules into the
// program, as `Rtx::shippedVulkanShaders`. Arrays of words, as ANGLE compiles its internal Vulkan shaders in, since
// `#embed` is no C++ MSVC reads and an assembler's `.incbin` none it has; the digest stated here, once, by the
// function a set read from a folder states its own by.

namespace
{
    /// How many words a line of the source holds.
    constexpr std::size_t sPerLine = 8;

    struct Module
    {
        std::string mName;
        std::vector<std::uint32_t> mWords;
    };

    std::string source(const std::vector<Module>& modules)
    {
        std::vector<Rtx::ShaderModule> views;
        views.reserve(modules.size());
        for (const Module& module : modules)
            views.push_back(Rtx::ShaderModule{ .mName = module.mName, .mWords = module.mWords });
        const Misc::DigestWords digest = Rtx::digestShaders(views);

        std::string text
            = "// Made by the build, by `openmw-rtx-spirv-embed`. Never edited.\n\n"
              "#include <array>\n#include <cstdint>\n\n"
              "#include <components/rtx/renderer/shaderset.hpp>\n"
              "#include <components/rtxvulkan/createrenderer.hpp>\n\n"
              "namespace Rtx\n{\n    namespace\n    {\n";
        for (std::size_t at = 0; at < modules.size(); ++at)
        {
            const std::vector<std::uint32_t>& words = modules[at].mWords;
            text += std::format(
                "        constinit const std::array<std::uint32_t, {}> sModule{}{{ {{\n", words.size(), at);
            for (std::size_t word = 0; word < words.size(); ++word)
            {
                text += word % sPerLine == 0 ? "            " : " ";
                text += std::format("0x{:08x},", words[word]);
                if (word % sPerLine == sPerLine - 1 || word + 1 == words.size())
                    text += '\n';
            }
            text += "        } };\n";
        }

        text += std::format("\n        constinit const std::array<ShaderModule, {}> sModules{{ {{\n", modules.size());
        for (std::size_t at = 0; at < modules.size(); ++at)
            text += std::format("            {{ \"{}\", sModule{} }},\n", modules[at].mName, at);
        text += "        } };\n\n";
        text += std::format(
            "        constinit const ShaderSet sShipped{{ .mModules = sModules, "
            ".mDigest = {{ {{ 0x{:016x}u, 0x{:016x}u }} }}, .mCensus = false }};\n",
            digest[0], digest[1]);
        text += "    }\n\n    const ShaderSet& shippedVulkanShaders()\n    {\n        return sShipped;\n    }\n}\n";
        return text;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: openmw-rtx-spirv-embed <source.cpp> <module.spv>...\n";
        return 2;
    }

    try
    {
        std::vector<Module> modules;
        for (const char* const argument : std::span(argv + 2, argv + argc))
        {
            const std::filesystem::path path(argument);
            std::string name = Rtx::spelledPath(path.filename());
            // Spelled into a string literal as it stands, so nothing that would need escaping.
            if (name.find_first_of("\"\\") != std::string::npos)
                throw std::runtime_error("a module named " + name + ", which a string literal cannot hold as it is");
            modules.push_back(Module{ .mName = std::move(name), .mWords = Rtx::readSpirv(path) });
        }
        // By name, which `ShaderSet::find` searches.
        std::ranges::sort(modules, {}, &Module::mName);

        const std::filesystem::path to(argv[1]);
        // Written whole, so a build stopped part way leaves no source it would take as made.
        const Result<void, Platform::IoError> written
            = Platform::writeWhole(to, source(modules), Platform::Durability::Whole);
        if (!written.isOk())
            throw std::filesystem::filesystem_error("could not write the shaders' source", to, written.error().mCode);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "openmw-rtx-spirv-embed: " << error.what() << '\n';
        return 1;
    }
}
