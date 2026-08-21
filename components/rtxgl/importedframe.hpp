#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RtxGl
{
    /// The sixteen bytes a driver identifies a physical device by.
    using DeviceUuid = std::array<std::uint8_t, 16>;

    /// Which device the current OpenGL context is running on.
    ///
    /// **Not a curiosity.** An allocation exported by Vulkan is only importable by the driver that
    /// made it, and a machine with two GPUs can easily have Vulkan on one and GL on the other — a
    /// laptop that did not switch, or a compositor that put the window on the integrated part. The
    /// import does not reliably fail in that case; it succeeds and the texture is rubbish.
    ///
    /// Empty where the context has no `GL_EXT_memory_object`, which is the same answer as "this
    /// context cannot import anything".
    std::string describeGlDevice();

    /// Whether the current context runs on the device with this UUID, and why not when it does not.
    ///
    /// The Vulkan side's is `VkPhysicalDeviceIDProperties::deviceUUID`.
    std::string findDeviceMismatch(const DeviceUuid& vulkan);

    /// A Vulkan image, as a texture in the current OpenGL context.
    ///
    /// **This is how the frame reaches the game's window.** The renderer does not present through a
    /// swapchain: the SDL window stays `SDL_WINDOW_OPENGL`, Vulkan renders offscreen into an image
    /// it exports, and GL imports the allocation and draws it under the MyGUI overlay. A native
    /// Vulkan window is a legitimate end state and it is not the way in — the character doll, the
    /// local map, the global map and video playback are all OSG render-to-texture users, and each
    /// would need reimplementing before the game was playable again (`docs/rtx/plan.md` §3).
    ///
    /// Nothing here synchronises. The frame has to be finished before the texture is read, and the
    /// renderer's own submit-and-wait is what makes that true today; a pipelined one will need a
    /// semaphore exported beside the memory.
    class ImportedFrame
    {
    public:
        /// Imports `memory` as a `GL_RGBA8` texture of `width` by `height`.
        ///
        /// @param memory a descriptor from `Rtx::Image::exportMemory`. **Ownership passes to GL**,
        ///        which closes it — unlike `glImportMemoryFdEXT`'s siblings for other handle types,
        ///        and unlike the Vulkan import, which only takes it on success.
        /// @param bytes the size the export reported, which is the driver's padded one and not
        ///        width times height times four.
        ///
        /// Throws `std::runtime_error` where the context cannot import, naming what is missing.
        ImportedFrame(int memory, std::uint64_t bytes, std::uint32_t width, std::uint32_t height);
        ~ImportedFrame();

        ImportedFrame(const ImportedFrame&) = delete;
        ImportedFrame& operator=(const ImportedFrame&) = delete;

        /// The GL texture name, for whatever draws it.
        unsigned int getTexture() const { return mTexture; }

        std::uint32_t getWidth() const { return mWidth; }
        std::uint32_t getHeight() const { return mHeight; }

        /// Reads the texture back, four bytes a pixel, tightly packed.
        ///
        /// For a test to compare against what Vulkan wrote. Nothing on the frame path does this.
        void read(std::vector<std::uint8_t>& pixels) const;

    private:
        unsigned int mMemory = 0;
        unsigned int mTexture = 0;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
    };
}
