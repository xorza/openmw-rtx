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

    /// Which upscaler runs: what an `UpscalerChoice` came to on this machine.
    enum class UpscalerKind
    {
        Fsr,
        Dlss,
    };

    /// Whether DLSS runs on this machine: each fact `Auto` and the menus read, in the order they are
    /// asked, and what the attempt came to once one was made.
    enum class DlssAvailability
    {
        /// The build has no DLSS backend (`DlssLibrary::sBuilt`).
        Unbuilt,

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

    /// Whether a menu offers DLSS under `availability`: where it runs, or where every fact holds and
    /// nothing tried it, and not where it was made and refused.
    constexpr bool offersDlss(const DlssAvailability availability)
    {
        return availability == DlssAvailability::Present || availability == DlssAvailability::Ready;
    }

    /// What `choice` comes to under `availability`: a concrete choice as it stands, so an explicit
    /// `Dlss` still tries and fails by name rather than quietly drawing through FSR; `Auto` is DLSS
    /// where it is offered, and FSR elsewhere.
    constexpr UpscalerKind chooseUpscaler(const UpscalerChoice choice, const DlssAvailability availability)
    {
        switch (choice)
        {
            case UpscalerChoice::Fsr:
                return UpscalerKind::Fsr;
            case UpscalerChoice::Dlss:
                return UpscalerKind::Dlss;
            case UpscalerChoice::Auto:
                break;
        }
        return offersDlss(availability) ? UpscalerKind::Dlss : UpscalerKind::Fsr;
    }

    /// The reconstruction between the trace and the display, whichever backend makes it. Both
    /// upscalers write an arena image and hand it to the display, so the rest of the frame never
    /// asks which one is running.
    ///
    /// **`Auto` is resolved where the device is known** (`chooseUpscaler`): DLSS where it is present,
    /// FSR everywhere else, and FSR as well where DLSS then refuses to start. An explicit `Dlss` that
    /// cannot start is an `UpscalerRefused` naming why, so a test never mistakes FSR's picture for
    /// DLSS's.
    class Upscaler
    {
    public:
        /// @param availability what `Auto` resolves by, which the renderer looked for once.
        /// @param application what NGX is told of the application, which outlives this; null in a
        ///        run that never asked for DLSS.
        Upscaler(const Device& device, UpscalerChoice choice, DlssAvailability availability,
            const Dlss::Application* application);

        Upscaler(const Upscaler&) = delete;
        Upscaler& operator=(const Upscaler&) = delete;

        /// What the choice came to.
        UpscalerKind getKind() const;

        /// Whether DLSS runs here, as the attempt left it where one was made.
        DlssAvailability getDlss() const { return mDlss; }

        /// Whether the trace writes the masks this upscaler reads: FSR reads them, and DLSS has no
        /// input for them.
        UpscaleMasks getMasks() const;

        /// The backend's `resize`: builds what one pair of extents keeps, and starts a history.
        void resize(Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        /// The images a reconstruction at these extents places in the frame's arena, appended to
        /// `into`.
        void declareShared(VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into) const;

        /// How many images `declareShared` declares.
        std::size_t getSharedCount() const;

        /// What `resize` to these extents builds in the device's memory, apart from the arena's images.
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
        HandedImage record(VkCommandBuffer commands, Uploads& uploads, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: which upscaler this renderer has.
        std::string_view describe() const;

    private:
        using Backend = std::variant<FsrUpscaler, DlssUpscaler>;

        static Backend makeBackend(const Device& device, UpscalerChoice choice, DlssAvailability& availability,
            const Dlss::Application* application);

        const Device& mDevice;

        /// Before the backend, which the constructor resolves it with.
        DlssAvailability mDlss;

        /// The one backend the constructor resolved, fixed for the run: a settings change asks for
        /// a restart, so nothing swaps it afterwards.
        Backend mBackend;
    };
}
