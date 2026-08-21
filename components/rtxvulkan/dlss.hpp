#pragma once

#include <span>
#include <string>

#include <vulkan/vulkan_core.h>

#include <components/rtx/upscale.hpp>

// NGX's own, `typedef struct X X` and not defined here: the SDK's headers are private to this
// target, and a test that builds a feature must be able to include this one.
struct NVSDK_NGX_Parameter;

namespace Rtx
{
    class Device;

    /// NVIDIA's NGX runtime, and what it says this machine can do with it.
    ///
    /// **Ray Reconstruction is an upscaler that denoises**, which is why it is worth a dependency
    /// rather than a filter of our own: it reconstructs from the demodulated radiance, albedo,
    /// normals, depth and motion the G-buffer already carries, across several frames, where the
    /// à-trous pass has one frame and one channel to work with.
    ///
    /// **One per device and one per process.** NGX keeps its state globally, keyed by the `VkDevice`
    /// it was brought up on, so this owns that lifetime: constructing it initialises and destroying
    /// it shuts down. Two of these alive on one device is not something the SDK promises to survive.
    ///
    /// Built only with `-DOPENMW_RTX_DLSS=ON`, which needs the SDK; the whole class is absent
    /// otherwise, so nothing has to ask at runtime whether it was compiled in.
    class Dlss
    {
    public:
        /// What NGX needs enabled on the instance and on the device, asked before either exists.
        ///
        /// **Static, and that is the only reason this can work.** The extensions have to be turned
        /// on when the instance and device are created, which is long before there is anything to
        /// initialise NGX with — so the SDK answers this without either.
        static std::span<const char* const> getInstanceExtensions();
        static std::span<const char* const> getDeviceExtensions();

        /// Starts NGX on this device. Throws `Error` where the runtime will not come up.
        Dlss(const Device& device, VkInstance instance);
        ~Dlss();

        Dlss(const Dlss&) = delete;
        Dlss& operator=(const Dlss&) = delete;

        /// Whether Ray Reconstruction can run here, which is a question about the driver as much as
        /// the hardware — NGX answers it after it has loaded its feature libraries and looked.
        bool isAvailable() const { return mAvailable; }

        /// Why not, where it cannot. Empty where it can.
        const std::string& getObstacle() const { return mObstacle; }

        /// What to render at to produce `output` under `upscale`, which must not be `Off`. Throws
        /// `Error` where DLSS will not answer, which is the same condition as it being unavailable.
        ///
        /// **DLSS's answer and not a ratio applied here.** The feature library picks the size, it
        /// has changed it between versions, and a frame traced at anything else is a frame it will
        /// refuse.
        VkExtent2D getRenderSize(VkExtent2D output, Upscale upscale) const;

        /// The device NGX was brought up on, which is the one a feature is built for.
        VkDevice getDevice() const { return mDevice; }

        /// NGX's own map, which answers questions. **Not the map a feature is built from** — that
        /// one is allocated per feature and belongs to it.
        NVSDK_NGX_Parameter* getCapabilities() const { return mCapabilities; }

    private:
        VkDevice mDevice = VK_NULL_HANDLE;
        NVSDK_NGX_Parameter* mCapabilities = nullptr;
        bool mAvailable = false;
        std::string mObstacle;
    };
}
