#pragma once

#include <cstdint>
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

    /// Every resource `module` declares a set and a binding for, into `into`, cleared first: the
    /// GLSL's own statement of each binding's type and count, read off the module the build wrote.
    /// A push block, which binds nothing, is not one.
    ///
    /// Throws `std::runtime_error` for words that are no module, or a resource of a type this does
    /// not know, naming it.
    void readBindings(std::span<const std::uint32_t> module, std::vector<ModuleBinding>& into);
}
