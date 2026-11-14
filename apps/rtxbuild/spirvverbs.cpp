#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <components/misc/digestwords.hpp>
#include <components/misc/result.hpp>
#include <components/platform/wholefile.hpp>
#include <components/rtx/renderer/shaderset.hpp>
#include <components/rtxvulkan/spirv/spirvdigest.hpp>
#include <components/rtxvulkan/spirv/spirvfile.hpp>
#include <components/rtxvulkan/spirv/spirvpin.hpp>

#include "verbs.hpp"

namespace RtxBuild
{
    namespace
    {
        /// Written whole (`Platform::writeWhole`), so a build stopped part way leaves the file as it was, or a
        /// source it would take as made, and never part of either.
        void writeWhole(const std::filesystem::path& path, const std::span<const std::byte> bytes)
        {
            const Result<void, Platform::IoError> written
                = Platform::writeWhole(path, bytes, Platform::Durability::Whole);
            if (!written.isOk())
                throw std::filesystem::filesystem_error("could not be written", path, written.error().mCode);
        }

        /// The module at `path` rewritten in place by `rewrite`, an error naming the module.
        template <class Rewrite>
        void rewriteModule(const std::filesystem::path& path, Rewrite&& rewrite)
        {
            // Named already: the reader says which file it could not read.
            const std::vector<std::uint32_t> words = Rtx::readSpirv(path);
            std::vector<std::uint32_t> rewritten;
            try
            {
                rewritten = rewrite(words);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(Rtx::spelledPath(path) + ": " + error.what());
            }
            writeWhole(path, std::as_bytes(std::span(rewritten)));
        }

        /// How many words a line of the embedded source holds.
        constexpr std::size_t sPerLine = 8;

        struct Module
        {
            std::string mName;
            std::vector<std::uint32_t> mWords;
        };

        /// Arrays of words, as ANGLE compiles its internal Vulkan shaders in, since `#embed` is no C++ MSVC reads
        /// and an assembler's `.incbin` none it has; the digest stated here, once, by the function a set read from a
        /// folder states its own by.
        std::string embeddedSource(const std::vector<Module>& modules)
        {
            std::vector<Rtx::ShaderModule> views;
            views.reserve(modules.size());
            for (const Module& module : modules)
                views.push_back(Rtx::ShaderModule{ .mName = module.mName, .mWords = module.mWords });
            const Misc::DigestWords digest = Rtx::digestShaders(views);

            std::string text
                = "// Made by the build, by `openmw-rtx-build embed`. Never edited.\n\n"
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

            text += std::format(
                "\n        constinit const std::array<ShaderModule, {}> sModules{{ {{\n", modules.size());
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

    void guard(const Arguments arguments)
    {
        rewriteModule(std::filesystem::path(arguments[0]),
            [](const std::vector<std::uint32_t>& words) { return Rtx::guardFloatArithmetic(words); });
    }

    void pin(const Arguments arguments)
    {
        rewriteModule(std::filesystem::path(arguments[0]),
            [](const std::vector<std::uint32_t>& words) { return Rtx::pinModule(words); });
    }

    void embed(const Arguments arguments)
    {
        std::vector<Module> modules;
        for (const char* const argument : arguments.subspan(1))
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

        const std::string text = embeddedSource(modules);
        writeWhole(std::filesystem::path(arguments[0]), std::as_bytes(std::span(text)));
    }

    void digest(Arguments /*arguments*/)
    {
        const std::string disassembly{ std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>() };
        const Misc::DigestWords digested = Rtx::digestProgram(disassembly);
        std::cout << std::format("{:016x}{:016x}\n", digested[0], digested[1]);
    }
}
