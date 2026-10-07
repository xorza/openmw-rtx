#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx
{
    class Device;
    class NotFinite;

    /// **The device's census of stores that were not finite** — `Shaders::Census`, which every
    /// census module counts into (`census.glsl`) — and the word each module counts into. One census
    /// for the device, which every layout binds at `BIND_CENSUS` and every push writes, and every
    /// stage is handed its word as a specialization constant: no pass binds it, writes it or is
    /// handed it.
    ///
    /// **A module's word is its place among the shader directory's modules, sorted by name**, read
    /// once as the census is made: the same word every run of one build, so a pipeline is the same
    /// pipeline to the driver's cache, whichever compile hand made it first.
    ///
    /// Made only where the device's shaders count (`ShaderSet::mCensus`), whose modules hold the
    /// census's code; the game's hold none of it.
    class NotFiniteCensus
    {
    public:
        static constexpr VkDeviceSize sBytes = sizeof(Shaders::Census);

        /// @param shaderDirectory where every module a pipeline on `device` is made from stands.
        NotFiniteCensus(const Device& device, const std::filesystem::path& shaderDirectory);

        /// The word of a stage built from `module`, a file of the shader directory, which it is
        /// specialized with (`SPEC_CENSUS_KERNEL`). Throws `InputError` for a module the directory
        /// did not hold.
        std::uint32_t kernelOf(std::string_view module) const;

        /// The census as the binding every layout of the device names takes it, for the next submit.
        VkDescriptorBufferInfo describe() const { return mWords.describe(); }

        /// Copies the census into the start of `into` and clears it, after everything recorded on
        /// the queue before and before everything after: what a frame owes as it ends, so what it
        /// reads back is what the queue counted since the frame before ended.
        void record(VkCommandBuffer commands, const Buffer& into) const;

        /// Adds what `copied`, a copy `record` made and the host may read, says each module wrote,
        /// by the module's name without its `.spv`, which the census keeps for as long as it lives.
        void readInto(const Buffer& copied, NotFinite& into) const;

    private:
        Buffer mWords;

        /// The directory's modules without their `.spv`, sorted: a module's word is its place.
        std::array<std::string, Shaders::CENSUS_KERNELS> mNames;
        std::uint32_t mNamed = 0;
    };
}
