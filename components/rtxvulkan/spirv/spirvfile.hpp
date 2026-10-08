#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <spirv/unified1/spirv.hpp>

#include <components/rtx/common/error.hpp>

namespace Rtx
{
    /// The words of the SPIR-V module at `path`, checked for being whole words and for beginning
    /// with the magic number: the one reader, for the modules the renderer loads and for the build's
    /// pin tool alike.
    ///
    /// @throws InputError naming `path` where it cannot be read or is not a module.
    std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path);

    /// `path` as UTF-8, as `Files::pathToUnicodeString` spells it: this library links nothing of the
    /// engine's, because every shader waits for the tool built from it.
    std::string spelledPath(const std::filesystem::path& path);

    /// The words a module's header takes, ahead of its first instruction.
    inline constexpr std::size_t sSpirvHeaderWords = 5;

    /// Hands `visit` each instruction of `module` after its header, in order: its opcode, its
    /// operands and the word it starts at. The one walk of a module's words, for what it states of
    /// its interface and for its pinning alike.
    ///
    /// @throws InputError for words that are no module in this machine's byte order, and for an
    ///         instruction that runs past the module.
    template <class Visit>
    void forEachInstruction(const std::span<const std::uint32_t> module, Visit&& visit)
    {
        if (module.size() < sSpirvHeaderWords || module[0] != spv::MagicNumber)
            throw InputError("not a SPIR-V module in this machine's byte order");

        for (std::size_t at = sSpirvHeaderWords; at < module.size();)
        {
            const std::uint32_t count = module[at] >> spv::WordCountShift;
            if (count == 0 || at + count > module.size())
                throw InputError(std::format("the instruction at word {} runs past the module", at));

            visit(static_cast<spv::Op>(module[at] & spv::OpCodeMask), module.subspan(at + 1, count - 1), at);
            at += count;
        }
    }
}
