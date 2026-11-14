#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/dlss/dlss.hpp>
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
    struct DriverProfile;
    class GpuTimer;

    /// Whether DLSS runs on this machine: each fact `Auto` and the menus read, in the order they are
    /// asked, and what the attempt came to once one was made.
    enum class DlssAvailability
    {
        /// The renderer's device is not NVIDIA's.
        NotNvidia,

        /// The player's feature library is not where NGX looks.
        NotInstalled,

        /// Every fact holds, and no DLSS was made: what `Auto` resolves to DLSS by.
        Present,

        /// DLSS was made and refused, so `Auto` came to FSR.
        Refused,

        /// DLSS runs.
        Ready,
    };

    /// NVIDIA DLSS Ray Reconstruction, through the NGX runtime the driver installs: one learned pass that denoises the
    /// noisy frame and upscales it, in place of the renderer's own filter and an upscaler after it
    /// (`UpscalerInput::Noisy`). The driver's core is opened at run time and the feature library is the player's; no
    /// NVIDIA code is linked or shipped. Evaluation is recorded into the frame's own command buffer, as FSR's
    /// dispatches are.
    ///
    /// The library's own default preset applies, as NVIDIA's guide recommends (DLSS-RR Integration Guide, 3.13), so a
    /// newer library the player installs brings its own defaults. The constructor is the answer to whether DLSS runs:
    /// it opens NGX for the device and creates a small feature, so every refusal is an `UpscalerRefused` there, where
    /// `Upscaler` can fall back from `auto`, and not at the first resize.
    class DlssUpscaler
    {
    public:
        /// Whether DLSS runs on a device of `driver` with the player's library looked for in
        /// `folders`, as far as the facts before an attempt say: the device first, then the library,
        /// whatever the run chose, so a run that chose FSR still offers DLSS for the next start.
        static DlssAvailability availabilityOn(const DriverProfile& driver, const DlssLibrary::Folders& folders);

        /// Throws `UpscalerRefused`, naming why, where the driver, the card or the library refuses.
        /// @param application what NGX is told of the application, which outlives this; null in a
        ///        run that never asked for DLSS, whose device is no candidate and is refused first.
        DlssUpscaler(const Device& device, const Dlss::Application* application);
        ~DlssUpscaler();

        DlssUpscaler(const DlssUpscaler&) = delete;
        DlssUpscaler& operator=(const DlssUpscaler&) = delete;
        DlssUpscaler(DlssUpscaler&&) = delete;
        DlssUpscaler& operator=(DlssUpscaler&&) = delete;

        /// The extents `mode` traces at for an output of `width` by `height`: NGX's optimal settings where it
        /// upscales, as the feature is made for, since Ray Reconstruction takes no extent but the one it gives
        /// (DLSS-RR Integration Guide 3.3), and the output where it does not. Throws `DeviceError` where NGX gives
        /// none.
        FrameExtents extentsFor(std::uint32_t width, std::uint32_t height, Upscale mode) const;

        /// Makes the feature at `mode`'s quality for one pair of extents, where it does not stand at
        /// them already, and measures what NGX holds for it (`bytesAt`): **ahead of the frame's
        /// reserve**, which counts it. Starts a history of its own. Throws `DeviceError` where NGX
        /// refuses the feature, which the constructor's own feature showed it would make.
        void prepare(Upscale mode, VkExtent2D render, VkExtent2D output);

        /// Measures what a feature at `mode` and these extents holds, for a reserve kept for a mode the
        /// run moves to (`RunProfile::mReservedUpscales`), by making one and letting it go; once per
        /// mode and extents at one output.
        void measure(Upscale mode, VkExtent2D render, VkExtent2D output);

        /// Takes `shared`, the images `declareShared` declared, as the frame's arena placed them, for
        /// the feature `prepare` made at these extents.
        void resize(Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        /// The images a reconstruction at these extents places in the frame's arena: the upscaled
        /// output, which the display reads, and the depth, the motion and the three guides the guide
        /// pass writes for NGX, which nothing past the upscale reads.
        static void declareShared(VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into);

        /// How many images `declareShared` declares.
        static std::size_t getSharedCount();

        /// What NGX holds on the device for a feature at these extents, as `prepare` or `measure`
        /// read it off NGX (`NGX_DLSSD_GET_STATS`): frame memory the driver allocates and the
        /// allocator does not, which the frame's reserve counts all the same. Nought where neither
        /// measured these extents.
        VkDeviceSize bytesAt(VkExtent2D render, VkExtent2D output) const;

        /// What the feature that stands holds, which the allocator is told it holds outside itself
        /// (`MemoryAllocator::holdOutside`): nought where none stands.
        VkDeviceSize getHeldOutside() const;

        /// Lets what `prepare` built go, the feature included.
        void release();

        bool isBuilt() const { return mTargets != nullptr; }
        bool isAt(VkExtent2D render, VkExtent2D output) const;

        void reset() { mFresh = true; }
        bool isFresh() const { return mFresh; }

        /// Records the guide pass and the NGX evaluation, and hands the output image to the display.
        /// After `resize`. The frame's constants are written in queue order ahead of the guide pass.
        HandedImage record(VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: the backend, and no version, since the version is the player's
        /// library and not the build's.
        static std::string_view describe();

    private:
        struct Context;
        struct Targets;

        /// What NGX held for a feature at one mode and pair of extents, as measured.
        struct Measured
        {
            VkExtent2D mRender{};
            VkExtent2D mOutput{};
            VkDeviceSize mBytes = 0;
        };

        /// Makes a feature at `mode` and these extents and reads what NGX holds for it: the stats
        /// after it stands less the stats before, so a feature standing beside it is not counted.
        Measured makeAndMeasure(Upscale mode, VkExtent2D render, VkExtent2D output, std::unique_ptr<Targets>& into);

        /// Keeps `measured`, dropping what was measured at another output: one entry a mode at most.
        void remember(const Measured& measured);

        static std::unique_ptr<Context> makeContext(const Device& device, const Dlss::Application* application);

        const Device& mDevice;

        /// NGX's state for this device, made in the constructor: a configuration that only ever
        /// runs FSR never constructs a DLSS upscaler and never loads the runtime.
        std::unique_ptr<Context> mContext;

        /// Empty until `resize`, and after `release`. After the context, so a feature is released
        /// before NGX shuts down.
        std::unique_ptr<Targets> mTargets;

        /// What `prepare` and `measure` read, at the current output, one a mode at most
        /// (`sUpscaleMenu`'s size bounds it).
        std::vector<Measured> mMeasured;

        bool mFresh = true;
    };
}
