#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

namespace Rtx
{
    class Device;

    /// SPIR-V from the device's shader directory, read once a file however many stages name it,
    /// and handed to each stage inline: `maintenance5` takes a stage's code in its `pNext`, so no
    /// `VkShaderModule` is made, named or destroyed. Kept by whoever makes the pipelines that share
    /// a file, and only until they are made: the driver reads the words during the create call.
    class ShaderCode
    {
    public:
        explicit ShaderCode(const Device& device);

        ShaderCode(const ShaderCode&) = delete;
        ShaderCode& operator=(const ShaderCode&) = delete;

        /// What the `pNext` of a stage that runs `module` points at, valid while this lives. The file
        /// is read on the first ask, and checked for being the one the build wrote, because a
        /// truncated `.spv` is otherwise a driver crash with no explanation.
        ///
        /// @throws InputError where the file cannot be read or is not a module.
        const void* stage(std::string_view module);

    private:
        /// One file's words and the structures a stage chains, which point into the words and at
        /// one another: a deque, so a later read moves none of them.
        struct Read
        {
            std::string mModule;
            std::vector<std::uint32_t> mWords;
            VkDebugUtilsObjectNameInfoEXT mNamed{};
            VkShaderModuleCreateInfo mCreate{};

            /// The head of the chain: the name where the device names objects, the code after it.
            const void* mStage = nullptr;
        };

        const Device& mDevice;
        std::deque<Read> mRead;
    };
}
