#include "lightbuilder.hpp"

#include <algorithm>
#include <cmath>

#include <components/esm3/loadligh.hpp>

namespace RtxBridge
{
    namespace
    {
        /// How bright a light is at half its recorded radius.
        ///
        /// There is no value in the record to be faithful to, so this is the whole of the scale and
        /// it was set by eye. Provisional in a specific way: vanilla textures have light painted
        /// into them already, so every lamp here is competing with illumination that is in the
        /// albedo, and this number only starts to mean something once that is unpicked.
        ///
        /// The pi is the Lambertian `1/pi` the shader divides by, kept here so the two cancel.
        constexpr float sIntensity = 0.25f * 3.14159265f;

        /// How much further a light reaches than its record says, and how much further again.
        ///
        /// Morrowind's radii run 64 to 256 units in an interior — a metre to three and a half at
        /// seventy units to the metre — so a lantern lights its own post and nothing else. That was
        /// a fixed falloff curve in a renderer with no bounce, where an ambient term filled the
        /// room; here the ambient is real light and the lamps have to be what lights the place.
        ///
        /// Scaling alone widens the gap it is meant to close: a candle's 64 units doubles to 128,
        /// which is still nothing, while a lantern's 256 gains a whole lantern's worth. The flat
        /// term narrows the two instead, and it is the candles that most need to leave their table.
        constexpr float sReachScale = 2.0f;
        constexpr float sReachBonus = 128.0f;
    }

    osg::Vec3f decodeColour(std::uint32_t packed)
    {
        const auto toLinear = [](std::uint32_t channel) {
            const float encoded = static_cast<float>(channel & 0xFFu) / 255.0f;
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        };

        return osg::Vec3f(toLinear(packed), toLinear(packed >> 8), toLinear(packed >> 16));
    }

    std::optional<Rtx::Light> makeLight(const ESM::Light& record, const osg::Vec3f& position)
    {
        constexpr int notPlaced = ESM::Light::Carry | ESM::Light::Negative | ESM::Light::OffDefault;
        if ((record.mData.mFlags & notPlaced) != 0)
            return std::nullopt;

        // The file is on disk and something else wrote it, so a nonsensical radius is data rather
        // than a broken contract: a light with no size lights nothing and is dropped.
        const auto radius = static_cast<float>(record.mData.mRadius);
        if (!(radius > 0.0f))
            return std::nullopt;

        return Rtx::Light{
            .mPosition = position,
            .mIntensity = decodeColour(record.mData.mColor) * (radius * radius * sIntensity),
            .mReach = radius * sReachScale + sReachBonus,
        };
    }
}
