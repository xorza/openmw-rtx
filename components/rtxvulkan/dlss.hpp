#pragma once

#include <span>
#include <string>

#include <vulkan/vulkan_core.h>

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

    private:
        VkDevice mDevice = VK_NULL_HANDLE;
        bool mAvailable = false;
        std::string mObstacle;
    };
}
