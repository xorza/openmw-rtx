#include "formatcensus.hpp"

#include <cassert>
#include <cstddef>

namespace Rtx
{
    void FormatCensus::count(const TextureFormat format, const bool mipped, const std::uint32_t pixelFormat)
    {
        FormatCount& met = mMet[static_cast<std::size_t>(format)];
        ++met.mMet;
        if (mipped)
            ++met.mMipped;

        if (format == TextureFormat::Unnamed)
            mUnnamed = pixelFormat;
    }

    void FormatCensus::discount(const TextureFormat format, const bool mipped)
    {
        FormatCount& met = mMet[static_cast<std::size_t>(format)];
        assert(met.mMet > 0 && "a texture discounted that was never counted");
        --met.mMet;
        if (mipped)
            --met.mMipped;
    }
}
