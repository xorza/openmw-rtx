#include "colour.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "shaders/colour.h"

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
