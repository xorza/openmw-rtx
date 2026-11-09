#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>

#include "dlssupscaler.hpp"
#include "fsrupscaler.hpp"
#include "upscaleinputs.hpp"

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// The reconstruction between the trace and the display, whichever backend makes it. Both
    /// upscalers write the same arena image and hand it to the display, so the rest of the frame
    /// never asks which one is running.
    ///
    /// **`Auto` is resolved where the device is known**: DLSS where this build has NGX and the card
    /// accepted it, FSR everywhere else. An explicit `Dlss` that cannot start is a start failure
    /// naming why, so a test never mistakes FSR's picture for DLSS's.
    class Upscaler
    {
    public:
        Upscaler(const Device& device, UpscalerKind requested);
        ~Upscaler();

        Upscaler(const Upscaler&) = delete;
        Upscaler& operator=(const Upscaler&) = delete;

        /// What `Auto` came to, which the renderer writes into its profile.
        UpscalerKind getKind() const { return mKind; }

        void resize(UpscalerKind kind, Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        static void declareShared(
            UpscalerKind kind, VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into);
        static std::size_t getSharedCount(UpscalerKind kind);
        static VkDeviceSize bytesAt(const Device& device, UpscalerKind kind, VkExtent2D render, VkExtent2D output);

        void release();

        bool isBuilt(UpscalerKind kind) const;
        bool isAt(UpscalerKind kind, VkExtent2D render, VkExtent2D output) const;

        void reset();
        bool isFresh() const;

        HandedImage record(UpscalerKind kind, VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: which upscaler this renderer has.
        std::string_view describe() const;

    private:
        /// Makes `kind`'s upscaler if it is not the one standing, letting the other go. `Auto` is
        /// never passed here: the constructor resolves it.
        void ensure(UpscalerKind kind);

        const Device& mDevice;
        UpscalerKind mKind;

        std::unique_ptr<FsrUpscaler> mFsr;
        std::unique_ptr<DlssUpscaler> mDlss;
    };
}
