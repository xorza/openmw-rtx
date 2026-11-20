#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    /// One of the kinds of memory a device offers, in the device's own order: which heap it is taken out of, how
    /// large that heap is, and whether it is the device's own and whether the host can map it.
    struct MemoryKind
    {
        std::uint32_t mHeap = 0;
        std::uint64_t mHeapBytes = 0;
        bool mOnDevice = false;
        bool mHostVisible = false;

        bool operator==(const MemoryKind& other) const = default;
    };

    /// What the device asks for one texture: a square of `mSide` texels in `mFormat` with its whole chain under it,
    /// which is also what a larger texture standing from that level asks. `mTexelBytes` is what its texels are, so
    /// the difference is the device's own.
    struct TexturePrice
    {
        TextureFormat mFormat = TextureFormat::Bc1RgbaSrgb;
        std::uint32_t mSide = 0;
        std::uint32_t mLevels = 0;
        std::uint64_t mTexelBytes = 0;
        std::uint64_t mBytes = 0;
        std::uint64_t mAlignment = 0;

        /// A bit a `MemoryKind` the device binds it to, the lowest the first kind.
        std::uint32_t mKinds = 0;

        bool operator==(const TexturePrice& other) const = default;
    };

    /// What the device asks for a mesh's structure as it is first built, before it is made tight: the structure and
    /// the scratch its build works in, for a mesh of `mTriangles` that does not deform.
    struct StructurePrice
    {
        std::uint32_t mTriangles = 0;
        std::uint64_t mBytes = 0;
        std::uint64_t mScratchBytes = 0;

        bool operator==(const StructurePrice& other) const = default;
    };

    /// **What a device charges for what content stands in**: the sizes and alignments its driver states, which
    /// decide how content's memory is cut up and are a driver's to state, not a constant of the renderer's. Asked
    /// once, of a device that draws nothing (`Renderer::priceMemory`).
    struct MemoryPrices
    {
        std::vector<MemoryKind> mKinds;

        /// The kinds the buffer a structure stands in may take, a bit a kind.
        std::uint32_t mStructureKinds = 0;

        /// How many allocations the device lets stand at once, and the least distance it asks between a buffer and
        /// an image in one allocation.
        std::uint32_t mAllocationsMost = 0;
        std::uint64_t mGranularity = 0;

        /// By format in `sPricedFormats`' order, then by side from one texel up.
        std::vector<TexturePrice> mTextures;

        /// By triangles, in `sPricedTriangles`' order.
        std::vector<StructurePrice> mStructures;
    };

    /// The formats priced: one of each layout a device stores a texture in, since the same blocks read as a colour
    /// or as data are one size.
    inline constexpr std::array sPricedFormats{ TextureFormat::Bc1RgbaSrgb, TextureFormat::Bc2Srgb,
        TextureFormat::Bc3Srgb, TextureFormat::Bc4Unorm, TextureFormat::Bc5Unorm, TextureFormat::Bc7Srgb,
        TextureFormat::Rgba8Srgb, TextureFormat::Bgra8Srgb };

    /// The mesh sizes priced, four times the last each: from a decal's two triangles' order to a town's largest.
    inline constexpr std::array<std::uint32_t, 10> sPricedTriangles{ 1, 4, 16, 64, 256, 1024, 4096, 16384, 65536,
        262144 };

    /// What the texels of a square of `side` in `format` come to with every level under it.
    std::uint64_t chainTexelBytes(TextureFormat format, std::uint32_t side);

    /// How many levels a square of `side` holds down to one texel.
    constexpr std::uint32_t chainLevels(std::uint32_t side)
    {
        std::uint32_t levels = 1;
        for (; side > 1; side /= 2)
            ++levels;
        return levels;
    }

    /// The prices as a report prints them, a line a price, a newline after each.
    std::string describe(const MemoryPrices& prices);
}
