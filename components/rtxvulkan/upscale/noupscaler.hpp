#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>

#include "upscaleinputs.hpp"

namespace Rtx
{
    class GpuTimer;

    /// No upscaler (`UpscalerChoice::Off`): a backend that holds nothing and is never recorded, since every frame of a
    /// renderer made with it traces at `Upscale::Off`, at the size it is shown at. What `Upscaler` stands where the
    /// player chose none, so the frame asks no question of it that the other two answer.
    class NoUpscaler
    {
    public:
        /// Every pixel traced, whatever `mode` says: no mode but `Off` reaches a renderer with no upscaler.
        static FrameExtents extentsFor(std::uint32_t width, std::uint32_t height, Upscale mode)
        {
            assert(mode == Upscale::Off && "a mode asked of no upscaler");
            return Rtx::extentsFor(width, height, mode);
        }

        static void prepare(Upscale, VkExtent2D, VkExtent2D) {}
        static void measure(Upscale, VkExtent2D, VkExtent2D) {}
        static VkDeviceSize getHeldOutside() { return 0; }
        static void resize(Upscale, VkExtent2D, VkExtent2D, const ArenaRun&) {}
        static void declareShared(VkExtent2D, VkExtent2D, std::vector<ArenaImage>&) {}
        static std::size_t getSharedCount() { return 0; }
        static VkDeviceSize bytesAt(VkExtent2D, VkExtent2D) { return 0; }
        static void release() {}
        static bool isBuilt() { return false; }
        static bool isAt(VkExtent2D, VkExtent2D) { return false; }
        static void reset() {}
        static bool isFresh() { return true; }

        /// Never called: a frame at `Upscale::Off` records no reconstruction, and there is no image to hand over.
        [[noreturn]] static HandedImage record(VkCommandBuffer, const UpscaleInputs&, GpuTimer*)
        {
            assert(false && "a frame recorded through no upscaler");
            std::abort();
        }

        static std::string_view describe() { return "none"; }
    };
}
