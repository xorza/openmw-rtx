#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>

#include "dlssupscaler.hpp"
#include "fsrupscaler.hpp"
#include "noupscaler.hpp"
#include "upscaleinputs.hpp"

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// Whether a menu offers DLSS under `availability`: where it runs, or where every fact holds and
    /// nothing tried it.
    constexpr bool offersDlss(const DlssAvailability availability)
    {
        return availability == DlssAvailability::Present || availability == DlssAvailability::Ready;
    }

    /// The reconstruction between the trace and the display, whichever backend makes it, or none. Both
    /// upscalers write an arena image and hand it to the display, so the rest of the frame never
    /// asks which one is running; under none, every frame is traced at `Upscale::Off`.
    ///
    /// **The choice is made as asked**: a `Dlss` that cannot start is an `UpscalerRefused` naming
    /// why, so a test never mistakes FSR's picture for DLSS's.
    class Upscaler
    {
    public:
        /// @param availability whether DLSS runs here, which the renderer looked for once.
        /// @param application what NGX is told of the application, which outlives this; null in a
        ///        run that never asked for DLSS.
        Upscaler(const Device& device, UpscalerChoice choice, DlssAvailability availability,
            const Dlss::Application* application);

        Upscaler(const Upscaler&) = delete;
        Upscaler& operator=(const Upscaler&) = delete;
        ~Upscaler() = default;

        /// Which upscaler this is.
        UpscalerChoice getChoice() const;

        /// Whether DLSS runs here, as the attempt left it where one was made.
        DlssAvailability getDlss() const { return mDlss; }

        /// Whether the trace writes the masks this upscaler reads: FSR reads them, and DLSS has no
        /// input for them.
        UpscaleMasks getMasks() const;

        /// What this upscaler takes from the trace, which decides who filters the light
        /// (`Reconstruction::resolve`).
        UpscalerInput getInput() const;

        /// How the resources of a runtime this upscaler drives are named, which records commands of
        /// its own into the frame's and synchronizes them itself (`ValidationLog::disown`): NGX's,
        /// under DLSS, and none under FSR, whose every command is the renderer's.
        std::optional<std::string_view> getRuntimeResources() const;

        /// What a frame shown at `width` by `height` is traced at under `mode`: FSR's arithmetic
        /// (`Rtx::extentsFor`), and under DLSS the extent NGX gives for the mode, which Ray
        /// Reconstruction takes and no other.
        FrameExtents extentsFor(std::uint32_t width, std::uint32_t height, Upscale mode) const;

        /// Makes what the upscaler holds at one pair of extents outside the frame's arena before the
        /// frame's reserve is set, which counts it: DLSS's feature, which NGX allocates and measures.
        /// FSR makes everything at `resize`.
        void prepare(Upscale mode, VkExtent2D render, VkExtent2D output);

        /// Measures what `prepare` would hold at `mode` and these extents, for a reserve kept for a mode
        /// the run moves to (`RunProfile::mReservedUpscales`).
        void measure(Upscale mode, VkExtent2D render, VkExtent2D output);

        /// What the upscaler holds now on the device that the allocator did not make, which the
        /// allocator is told (`MemoryAllocator::holdOutside`).
        VkDeviceSize getHeldOutside() const;

        /// The backend's `resize`: builds what one pair of extents keeps, after `prepare`, and starts a
        /// history.
        void resize(Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        /// The images a reconstruction at these extents places in the frame's arena, appended to
        /// `into`.
        void declareShared(VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into) const;

        /// How many images `declareShared` declares.
        std::size_t getSharedCount() const;

        /// What `prepare` and `resize` to these extents build in the device's memory, apart from the
        /// arena's images: DLSS's as `prepare` or `measure` measured it.
        VkDeviceSize bytesAt(VkExtent2D render, VkExtent2D output) const;

        /// Lets what `resize` built go, for a mode turned off that may come back.
        void release();

        bool isBuilt() const;
        bool isAt(VkExtent2D render, VkExtent2D output) const;

        /// Says the history is worthless until the next `record`, as after a jump.
        void reset();

        /// Whether the next `record` reads no history.
        bool isFresh() const;

        /// Records one reconstruction at the output extent and hands over the image it wrote.
        HandedImage record(VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: which upscaler this renderer has.
        std::string_view describe() const;

    private:
        using Backend = std::variant<NoUpscaler, FsrUpscaler, DlssUpscaler>;

        static Backend makeBackend(const Device& device, UpscalerChoice choice, DlssAvailability& availability,
            const Dlss::Application* application);

        /// Before the backend, which the constructor makes it Ready by.
        DlssAvailability mDlss;

        /// The one backend the constructor resolved, fixed for the run: a settings change asks for
        /// a restart, so nothing swaps it afterwards.
        Backend mBackend;
    };
}
