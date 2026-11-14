#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    class Buffer;

    /// What a placement holds of its sprites, for a bin to copy and shade: the pristine table the
    /// host wrote and the emitters that placed them.
    struct SpriteSource
    {
        const Buffer* mSprites = nullptr;
        VkDeviceAddress mEmitters = 0;
        std::uint32_t mSpriteCount = 0;
        std::uint32_t mEmitterCount = 0;

        /// Where the placement's medium and additive instances can be met, `Shaders::GpuPresence`.
        VkDeviceAddress mPresences = 0;
        std::uint32_t mPresenceCount = 0;

        /// How many of the sprites a puff's light is asked for (`puffLight`): every one but an additive emitter's,
        /// whose glow is its own.
        std::uint32_t mLitSpriteCount = 0;
    };
}
