#include "colour.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <components/rtx/shaders/colour.h>

namespace Rtx
{
    namespace
    {
        /// The two hundred and fifty-six answers there are, worked out on the first ask so that no
        /// order between translation units can put a reader before it.
        const std::array<float, 256>& ofByte()
        {
            static const std::array<float, 256> sMade = [] {
                std::array<float, 256> made{};
                for (std::size_t at = 0; at < made.size(); ++at)
                    made[at] = Shaders::decodeSrgb(static_cast<float>(at) / 255.0f);

                return made;
            }();

            return sMade;
        }

        /// Where `encodedByte` reads each byte off: the least float that encodes to each byte, and the byte
        /// each of `sBuckets` even steps of the unit range starts at, which no byte's span is narrower than.
        /// Made on the first ask, as `ofByte` is.
        struct EncodedStarts
        {
            static constexpr std::size_t sBuckets = 4096;

            std::array<float, 256> mLeast;
            std::array<std::uint8_t, sBuckets> mBucket;
        };

        const EncodedStarts& encodedStarts()
        {
            static const EncodedStarts sMade = [] {
                EncodedStarts made{};
                made.mLeast[0] = -std::numeric_limits<float>::infinity();

                // **Bisected on the float's bits**, which order the non-negative floats as their values
                // do, so each start is the exact least float whose byte is at least it: the encode is
                // monotone, held to nought below and to 255 from one on.
                const auto byteOf = [](std::uint32_t bits) {
                    return std::to_integer<std::uint32_t>(unitByte(toEncoded(std::bit_cast<float>(bits))));
                };
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
                    made.mLeast[byte] = std::bit_cast<float>(low);
                }

                std::uint32_t byte = 0;
                for (std::size_t bucket = 0; bucket < EncodedStarts::sBuckets; ++bucket)
                {
                    const float start = static_cast<float>(bucket) / EncodedStarts::sBuckets;
                    while (byte < 255 && start >= made.mLeast[byte + 1])
                        ++byte;
                    made.mBucket[bucket] = static_cast<std::uint8_t>(byte);
                }
                return made;
            }();

            return sMade;
        }
    }

    float toLinear(float encoded)
    {
        // The table where the value arrived as a stored byte, because nearly everything the
        // content states is `k / 255` and `std::pow` is a libm call no compiler inlines. Exact
        // rather than a guess, because the comparison is the same `k / 255.0f` the table was
        // built from, and anything else takes the curve. The bounds are asked first so that a NaN
        // never reaches the conversion to an integer.
        if (encoded > 0.0f && encoded <= 1.0f)
        {
            const auto byte = static_cast<std::uint8_t>(encoded * 255.0f + 0.5f);
            if (static_cast<float>(byte) / 255.0f == encoded)
                return ofByte()[byte];
        }

        return Shaders::decodeSrgb(encoded);
    }

    float toEncoded(float linear)
    {
        return std::clamp(Shaders::encodeSrgb(linear), 0.0f, 1.0f);
    }

    std::byte encodedByte(const float linear)
    {
        // The bucket's start, then up past each byte's least float the value reaches: a step or two
        // where the curve is steepest. Asked as `>`, so a NaN stays at nought, as the encode answers it.
        const EncodedStarts& starts = encodedStarts();
        const std::size_t bucket = linear > 0.0f
            ? std::min(static_cast<std::size_t>(std::min(linear, 1.0f) * EncodedStarts::sBuckets),
                  EncodedStarts::sBuckets - 1)
            : 0;
        std::uint32_t byte = starts.mBucket[bucket];
        while (byte < 255 && linear >= starts.mLeast[byte + 1])
            ++byte;
        return static_cast<std::byte>(byte);
    }

    float toLinear(std::uint8_t encoded)
    {
        return ofByte()[encoded];
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
