#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>

#include "upscaleinputs.hpp"

namespace Rtx::Dlss
{
    class Application;
}

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// NVIDIA DLSS Super Resolution, through the NGX runtime the driver installs. The driver's core
    /// is opened at run time and the feature library is the player's; no NVIDIA code is linked or
    /// shipped. A card and driver NGX accepts at start, and a feature library where NGX looks for
    /// it, are what this class needs. Feature creation and evaluation are recorded into the frame's
    /// own command buffer, as FSR's dispatches are.
    ///
    /// **The library's own default preset applies**: no preset hints are set here, so a newer
    /// library the player installs brings its own defaults.
    ///
    /// **The constructor is the answer to whether DLSS runs**: it opens NGX for the device and
    /// creates a small feature, which loads the player's library, so every refusal is an
    /// `Unsupported` there, where `Upscaler` can fall back from `auto`, and not at the first resize.
    class DlssUpscaler
    {
    public:
        /// Throws `Unsupported`, naming why, where the driver, the card or the library refuses.
        /// @param application what NGX is told of the application, which outlives this; null in a
        ///        run that never asked for DLSS, whose device is no candidate and is refused first.
        DlssUpscaler(const Device& device, const Dlss::Application* application);
        ~DlssUpscaler();

        DlssUpscaler(const DlssUpscaler&) = delete;
        DlssUpscaler& operator=(const DlssUpscaler&) = delete;

        /// Builds what one pair of extents keeps, the feature at `mode`'s quality among it, and starts
        /// a history of its own. Takes `shared`, the image `declareShared` declared, as the frame's
        /// arena placed it. What stood before stands until the new feature is made, so a refusal
        /// leaves the last extents' targets whole.
        void resize(Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        /// The one image a reconstruction at these extents writes and no later frame reads, which
        /// the frame's arena places: the upscaled output, which the display reads.
        static void declareShared(VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into);

        /// How many images `declareShared` declares.
        static std::size_t getSharedCount();

        /// What `resize` to these extents builds in the device's memory, apart from the shared
        /// image the arena holds.
        static VkDeviceSize bytesAt(const Device& device, VkExtent2D render, VkExtent2D output);

        /// Lets what `resize` built go, the feature included.
        void release();

        bool isBuilt() const { return mTargets != nullptr; }
        bool isAt(VkExtent2D render, VkExtent2D output) const;

        void reset() { mFresh = true; }
        bool isFresh() const { return mFresh; }

        /// Records the input resolve and the NGX evaluation, and hands the output image to the
        /// display. After `resize`.
        HandedImage record(VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: the backend, and no version, since the version is the player's
        /// library and not the build's.
        static std::string_view describe();

    private:
        struct Context;
        struct Targets;

        static std::unique_ptr<Context> makeContext(const Device& device, const Dlss::Application* application);

        const Device& mDevice;

        /// NGX's state for this device, made in the constructor: a configuration that only ever
        /// runs FSR never constructs a DLSS upscaler and never loads the runtime.
        std::unique_ptr<Context> mContext;

        /// Empty until `resize`, and after `release`. After the context, so a feature is released
        /// before NGX shuts down.
        std::unique_ptr<Targets> mTargets;

        bool mFresh = true;
    };
}
