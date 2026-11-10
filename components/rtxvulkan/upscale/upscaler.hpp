#pragma once

#include <cstddef>
#include <string_view>
#include <variant>
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
    class Uploads;

    /// The reconstruction between the trace and the display, whichever backend makes it. Both
    /// upscalers write an arena image and hand it to the display, so the rest of the frame never
    /// asks which one is running.
    ///
    /// **`Auto` is resolved where the device is known** (`resolveUpscaler`): DLSS where this build
    /// has NGX, the card is an NVIDIA candidate and the player's library is installed, FSR
    /// everywhere else, and FSR as well where DLSS then refuses to start. An explicit `Dlss` that
    /// cannot start is a start failure naming why, so a test never mistakes FSR's picture for DLSS's.
    class Upscaler
    {
    public:
        /// @param readiness what `Auto` resolves by, which the renderer looked for once.
        /// @param application what NGX is told of the application, which outlives this; null in a
        ///        run that never asked for DLSS.
        Upscaler(const Device& device, UpscalerKind requested, const DlssReadiness& readiness,
            const Dlss::Application* application);

        Upscaler(const Upscaler&) = delete;
        Upscaler& operator=(const Upscaler&) = delete;

        /// What the request came to: `Fsr` or `Dlss`, never `Auto`.
        UpscalerKind getKind() const;

        /// The backend's `resize`: builds what one pair of extents keeps, and starts a history.
        void resize(Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        /// The images a reconstruction by `kind`, resolved, at these extents places in the frame's
        /// arena, appended to `into`.
        static void declareShared(
            UpscalerKind kind, VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into);

        /// How many images `declareShared` declares for `kind`.
        static std::size_t getSharedCount(UpscalerKind kind);

        /// What `resize` to these extents builds for `kind` in the device's memory, apart from the
        /// arena's images.
        static VkDeviceSize bytesAt(const Device& device, UpscalerKind kind, VkExtent2D render, VkExtent2D output);

        /// Lets what `resize` built go, for a mode turned off that may come back.
        void release();

        bool isBuilt() const;
        bool isAt(VkExtent2D render, VkExtent2D output) const;

        /// Says the history is worthless until the next `record`, as after a jump.
        void reset();

        /// Whether the next `record` reads no history.
        bool isFresh() const;

        /// Records one reconstruction at the output extent and hands over the image it wrote.
        HandedImage record(VkCommandBuffer commands, Uploads& uploads, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: which upscaler this renderer has.
        std::string_view describe() const;

    private:
        using Backend = std::variant<FsrUpscaler, DlssUpscaler>;

        static Backend makeBackend(const Device& device, UpscalerKind requested, const DlssReadiness& readiness,
            const Dlss::Application* application);

        /// The one backend the constructor resolved, fixed for the run: a settings change asks for
        /// a restart, so nothing swaps it afterwards.
        Backend mBackend;
    };
}
