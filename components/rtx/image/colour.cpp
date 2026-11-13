#include "colour.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <components/rtx/shaders/colour.h>

namespace Rtx
{
    DisplayCurve::DisplayCurve()
    {
        for (std::size_t at = 0; at < mLinear.size(); ++at)
            mLinear[at] = Shaders::decodeSrgb(static_cast<float>(at) / 255.0f);

        // **Bisected on the float's bits**, which order the non-negative floats as their values
        // do, so each start is the exact least float whose byte is at least it: the encode is
        // monotone, held to nought below and to 255 from one on.
        const auto byteOf = [](std::uint32_t bits) {
            return std::to_integer<std::uint32_t>(unitByte(toEncoded(std::bit_cast<float>(bits))));
        };
        mLeast[0] = -std::numeric_limits<float>::infinity();
        mLeast[256] = std::numeric_limits<float>::infinity();
        for (std::uint32_t byte = 1; byte < 256; ++byte)
        {
            std::uint32_t low = 0;
            std::uint32_t high = std::bit_cast<std::uint32_t>(1.0f);
            while (low < high)
            {
                const std::uint32_t middle = low + (high - low) / 2;
                if (byteOf(middle) >= byte)
                    high = middle;
                else
                    low = middle + 1;
            }
            mLeast[byte] = std::bit_cast<float>(low);
        }

        std::uint32_t byte = 0;
        for (std::size_t bucket = 0; bucket < sBuckets; ++bucket)
        {
            const float start = static_cast<float>(bucket) / sBuckets;
            while (byte < 255 && start >= mLeast[byte + 1])
                ++byte;
            mBucket[bucket] = static_cast<std::uint8_t>(byte);

            // What `encodedByte`'s one step rests on: the byte after the next starts past this bucket.
            assert(byte == 255 || mLeast[byte + 2] >= static_cast<float>(bucket + 1) / sBuckets);
        }
    }

    float toLinear(float encoded)
    {
        // The table where the value arrived as a stored byte: `std::pow` is a libm call no compiler inlines. Exact,
        // because the comparison is the same `k / 255.0f` the table was built from. The bounds are asked first so that
        // a NaN never reaches the conversion to an integer.
        if (encoded > 0.0f && encoded <= 1.0f)
        {
            const auto byte = static_cast<std::uint8_t>(encoded * 255.0f + 0.5f);
            if (static_cast<float>(byte) / 255.0f == encoded)
                return DisplayCurve::get().toLinear(byte);
        }

        return Shaders::decodeSrgb(encoded);
    }

    float toEncoded(float linear)
    {
        return std::clamp(Shaders::encodeSrgb(linear), 0.0f, 1.0f);
    }

    osg::Vec3f toLinear(const osg::Vec3f& encoded)
    {
        return osg::Vec3f(toLinear(encoded.x()), toLinear(encoded.y()), toLinear(encoded.z()));
    }

    osg::Vec3f decodeColour(const osg::Vec4f& encoded)
    {
        return toLinear(osg::Vec3f(encoded.x(), encoded.y(), encoded.z()));
    }

    osg::Vec3f decodeColour(const osg::Vec4ub& encoded)
    {
        return osg::Vec3f(toLinear(encoded.r()), toLinear(encoded.g()), toLinear(encoded.b()));
    }

    osg::Vec3f decodeColour(const EncodedColour& encoded)
    {
        return toLinear(osg::Vec3f(encoded.mRed, encoded.mGreen, encoded.mBlue));
    }

    osg::Vec3f decodeColour(std::uint32_t packed)
    {
        const auto channel = [](std::uint32_t bits) { return toLinear(static_cast<std::uint8_t>(bits & 0xFFu)); };

        return osg::Vec3f(channel(packed), channel(packed >> 8), channel(packed >> 16));
    }
}
