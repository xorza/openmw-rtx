#include "memoryprices.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    namespace
    {
        /// The kinds `bits` names, as a report lists them: "0, 1, 7".
        std::string describeKinds(const std::uint32_t bits)
        {
            std::string text;
            for (std::uint32_t kind = 0; kind < 32; ++kind)
                if ((bits & (1u << kind)) != 0)
                    text += std::format("{}{}", text.empty() ? "" : ", ", kind);
            return text.empty() ? std::string("none") : text;
        }
    }

    std::uint64_t chainTexelBytes(const TextureFormat format, const std::uint32_t side)
    {
        const TexelLayout layout = layoutOf(format);
        std::uint64_t bytes = 0;
        for (std::uint32_t at = side; at >= 1; at /= 2)
            bytes += layout.levelBytes(at, at);
        return bytes;
    }

    std::string describe(const std::span<const CopyRate> rates)
    {
        std::string text
            = "copies of BC7 chains on the device: the chains, their texels, the time, the rate, one chain's time\n";
        for (const CopyRate& rate : rates)
        {
            const double gibPerSecond = rate.mMs > 0.0
                ? static_cast<double>(rate.mBytes) / (rate.mMs / 1000.0) / static_cast<double>(std::uint64_t{ 1 } << 30)
                : 0.0;
            text += std::format(
                "  {:>5}  {:>5} chains  {:>10} bytes  {:>8.3f} ms  {:>6.1f} GiB/s  {:>8.4f} ms a chain\n", rate.mSide,
                rate.mCopies, rate.mBytes, rate.mMs, gibPerSecond, rate.mCopies > 0 ? rate.mMs / rate.mCopies : 0.0);
        }
        return text;
    }

    std::string describe(const MemoryPrices& prices)
    {
        std::string text = "kinds of memory\n";
        for (std::size_t kind = 0; kind < prices.mKinds.size(); ++kind)
            text += std::format("  {:>2}  heap {}, {:.0f} MiB{}{}\n", kind, prices.mKinds[kind].mHeap,
                mebibytes(prices.mKinds[kind].mHeapBytes), prices.mKinds[kind].mOnDevice ? ", the device's own" : "",
                prices.mKinds[kind].mHostVisible ? ", mapped by the host" : "");
        text += std::format("  a structure's buffer takes kinds {}\n", describeKinds(prices.mStructureKinds));
        text += std::format("  {} allocations at most, {} bytes between a buffer and an image\n",
            prices.mAllocationsMost, prices.mGranularity);

        text += "\ntextures, a square with its whole chain: the device's bytes, the texels', what is over, the "
                "alignment, the kinds\n";
        for (const TexturePrice& price : prices.mTextures)
            text += std::format("  {:<18} {:>5}  {:>2} levels  {:>10}  {:>10}  {:>8}  {:>7}  {}\n",
                nameOf(price.mFormat), price.mSide, price.mLevels, price.mBytes, price.mTexelBytes,
                price.mBytes - std::min(price.mBytes, price.mTexelBytes), price.mAlignment,
                describeKinds(price.mKinds));

        text += "\nstructures as first built, by triangles: the structure's bytes, a triangle's share, the build's "
                "scratch\n";
        for (const StructurePrice& price : prices.mStructures)
            text += std::format("  {:>7}  {:>10}  {:>8.1f}  {:>10}\n", price.mTriangles, price.mBytes,
                static_cast<double>(price.mBytes) / static_cast<double>(std::max(price.mTriangles, 1u)),
                price.mScratchBytes);
        return text;
    }
}
