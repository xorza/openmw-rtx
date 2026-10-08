#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>

#include <components/rtxvulkan/spirv/spirvinterface.hpp>

namespace Rtx
{
    class Device;

    /// The first binding a module declares that `sets` — its pipeline layout's, each set as its
    /// layout states it, the census left out of `SET_PASS` — states another way, in words: one in a
    /// set the layout does not name, one its set does not declare, or one declared of another type
    /// or count. Nothing where they agree. A binding the layout declares and the module does not
    /// read is no disagreement: a stage reads the part of a set it needs, and a variant that counts
    /// nothing binds no census.
    std::optional<std::string> bindingDisagreement(std::span<const ModuleBinding> module, const SetTables& sets);

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
        /// **And the module's bindings held to `sets`**, the layout's statement of every set, which
        /// the C++ writes beside the GLSL: a set or a binding the layout does not state, or a type
        /// or a count the two state differently, ends the process as a crash naming the module and
        /// the binding (`bindingDisagreement`), where the device would read a resource as another.
        ///
        /// @throws InputError where the file cannot be read or is not a module.
        const void* stage(std::string_view module, const SetTables& sets);

    private:
        /// Reads `module` onto the end of `mRead`.
        void readModule(std::string_view module);

        /// One file's words and the structures a stage chains, which point into the words and at
        /// one another: a deque, so a later read moves none of them.
        struct Read
        {
            std::string mModule;
            std::vector<std::uint32_t> mWords;
            ModuleInterface mInterface;
            VkDebugUtilsObjectNameInfoEXT mNamed{};
            VkShaderModuleCreateInfo mCreate{};

            /// The head of the chain: the name where the device names objects, the code after it.
            const void* mStage = nullptr;
        };

        const Device& mDevice;
        std::deque<Read> mRead;
    };
}
