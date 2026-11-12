#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <components/rtx/renderer/shaderset.hpp>

namespace Rtx
{
    /// The compiled shaders of a folder the build wrote, read whole once and held: a harness's sets and the
    /// tests', which run only from the tree that built them. The game's are compiled into it
    /// (`shippedVulkanShaders`).
    class ShaderFolder
    {
    public:
        /// Reads every `.spv` in `folder`, whose modules count into the census where `census` says they do.
        ///
        /// @throws InputError where the folder or a module in it cannot be read, or a file is not a module.
        ShaderFolder(const std::filesystem::path& folder, bool census);

        /// Pinned: the set points into what this holds.
        ShaderFolder(const ShaderFolder&) = delete;
        ShaderFolder& operator=(const ShaderFolder&) = delete;
        ShaderFolder(ShaderFolder&&) = delete;
        ShaderFolder& operator=(ShaderFolder&&) = delete;
        ~ShaderFolder() = default;

        const ShaderSet& getSet() const { return mSet; }

    private:
        /// Every module's words end to end, and each one's name.
        std::vector<std::uint32_t> mWords;
        std::vector<std::string> mNames;

        std::vector<ShaderModule> mModules;
        ShaderSet mSet;
    };
}
