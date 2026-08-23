#pragma once

#include <memory>
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
    /// **One per process, and the type is what makes that true.** NGX keeps its state globally and
    /// `NVSDK_NGX_VULKAN_Shutdown` is unconditional — the second of these to be destroyed does not
    /// put the first one's state back, it ends it. So there is no public constructor: `open` hands
    /// out a share of the one runtime and the last handle to go is what shuts it down.
    ///
    /// **That is not a style preference, it is the bug this shape exists to prevent.** A second one
    /// built to answer "is Ray Reconstruction available" and let go again leaves the first holding a
    /// feature it can no longer evaluate, and what that looks like is `FAIL_NotInitialized` from a
    /// frame several seconds later with nothing pointing back at the question.
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

        /// The process's NGX runtime, started if nothing is holding one already.
        ///
        /// **A share and not an instance.** Everything that wants to ask NGX something holds one of
        /// these for as long as it is asking, and the runtime comes down when the last one goes.
        /// Callers on one thread, which every caller is: a renderer is built before there is
        /// anything else to build one from.
        ///
        /// Throws `Error` where the runtime will not come up, and where a second device asks for it
        /// — there is one runtime and it belongs to the device that started it.
        static std::shared_ptr<const Dlss> open(const Device& device, VkInstance instance);

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
        Dlss(const Device& device, VkInstance instance);

        /// Whoever is holding the runtime, or nothing where it is down. Not owning: the handles
        /// `open` hands out are what keep it up, so the last one to go is what shuts it down.
        static inline std::weak_ptr<Dlss> sOpen;

        VkDevice mDevice = VK_NULL_HANDLE;
        NVSDK_NGX_Parameter* mCapabilities = nullptr;
        bool mAvailable = false;
        std::string mObstacle;
    };
}
