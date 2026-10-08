#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Rtx
{
    /// What a module's resource is, as SPIR-V spells it: the half of a descriptor's type the shader
    /// states. A layout states the other half, so the two can be held to each other.
    enum class DescriptorKind : std::uint8_t
    {
        /// A sampler on its own, which no image comes with.
        BareSampler,
        SampledImage,
        CombinedImageSampler,
        StorageImage,
        UniformTexelBuffer,
        StorageTexelBuffer,
        UniformBuffer,
        StorageBuffer,
        AccelerationStructure,
    };

    /// One resource a module binds: its set, its binding, what it is, and how many — the length of
    /// an array of them, one for a single one, and nought for an array of no stated length.
    struct ModuleBinding
    {
        std::uint32_t mSet = 0;
        std::uint32_t mBinding = 0;
        DescriptorKind mKind = DescriptorKind::BareSampler;
        std::uint32_t mCount = 1;

        bool operator==(const ModuleBinding& other) const = default;
    };

    /// What a specialization constant takes: a `bool`, which a word of nought or one specializes,
    /// or a 32-bit number, which any word does.
    enum class SpecKind : std::uint8_t
    {
        Bool,
        Word,
    };

    /// One specialization constant a module declares: the id its `constant_id` names, and its kind.
    struct ModuleSpecConstant
    {
        std::uint32_t mId = 0;
        SpecKind mKind = SpecKind::Word;

        bool operator==(const ModuleSpecConstant& other) const = default;
    };

    /// One input a stage reads at a location, and how many 32-bit components it reads there.
    struct ModuleInput
    {
        std::uint32_t mLocation = 0;
        std::uint32_t mComponents = 0;

        bool operator==(const ModuleInput& other) const = default;
    };

    /// What a module states of how it is fed, read off the module the build wrote: the GLSL's own
    /// statement of each, which the C++ that feeds it is held to.
    struct ModuleInterface
    {
        /// Every resource with a set and a binding. A push block, which binds nothing, is not one.
        std::vector<ModuleBinding> mBindings;

        std::vector<ModuleSpecConstant> mSpecConstants;

        /// Where the push block's last member ends, in bytes: the bytes the module reads of a push,
        /// through its nested structures, arrays and matrices. Nothing for a module with no push
        /// block.
        std::optional<std::uint32_t> mPushEnd;

        /// The inputs at a location, which a vertex stage's attributes feed. A built-in is none.
        std::vector<ModuleInput> mInputs;
    };

    /// `module`'s interface, in one walk of its words.
    ///
    /// @throws InputError for words that are no module, or a resource, a specialization constant, a
    ///         push block's member or an input of a type this does not know, naming it: the module
    ///         is the installation's, as its file is (`readSpirv`).
    ModuleInterface readInterface(std::span<const std::uint32_t> module);
}
