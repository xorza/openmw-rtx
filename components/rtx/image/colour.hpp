#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/Vec4ub>

namespace Rtx
{
    /// A colour as a content file states one: three channels from nought to one, display-encoded.
    /// A type, because the space is the whole of what a reader gets wrong: an `osg::Vec3f` says
    /// nothing about which side of `Rtx::decodeColour` a value is on. No arithmetic, on purpose —
    /// a gain belongs past the decode, where the numbers are light.
    struct EncodedColour
    {
        float mRed = 0.0f;
        float mGreen = 0.0f;
        float mBlue = 0.0f;

        bool operator==(const EncodedColour& other) const = default;
    };

    /// sRGB's transfer function, and its inverse clamped to the unit range. Whatever is averaged is averaged between
    /// these two: half of one ground type and half of another meet at 188 in light and at 128 in bytes. A value that is
    /// one of the 256 is answered from the table below whichever overload is called, so the two answers agree.
    float toLinear(float encoded);
    float toEncoded(float linear);

    /// The display curve read off tables, both ways: the light a stored byte decodes to, and the byte a light is
    /// stored as. Read inline, since a painted chain's texel asks six times
    /// (`docs/research/2026-11-13-preparation-cpu-by-codec.md`), and a loop takes `get` once ahead of its texels.
    class DisplayCurve
    {
    public:
        /// The one there is, made on the first ask, so that no order between translation units can put a
        /// reader before it.
        static const DisplayCurve& get()
        {
            static const DisplayCurve sMade;
            return sMade;
        }

        /// The light a stored byte decodes to: the two hundred and fifty-six answers there are, since
        /// `std::pow` is a libm call.
        float toLinear(std::uint8_t encoded) const { return mLinear[encoded]; }

        /// `unitByte(toEncoded(linear))`, read off where each byte starts, which are found once from those
        /// two: the same byte for every float on every platform, at a ninth of `std::pow`'s cost. **The
        /// bucket's start, and one more where the value reaches the next byte's least float**, with no
        /// branch on the value: bytes start further apart than a bucket is wide, so no bucket holds two
        /// starts, which the tables' making asserts.
        /// Asked as `>` and `>=`, so a NaN stays at nought, as the encode answers it.
        std::byte encodedByte(float linear) const
        {
            const std::size_t bucket = linear > 0.0f
                ? std::min(static_cast<std::size_t>(std::min(linear, 1.0f) * sBuckets), sBuckets - 1)
                : 0;
            const std::uint32_t byte = mBucket[bucket];
            return static_cast<std::byte>(byte + (linear >= mLeast[byte + 1] ? 1u : 0u));
        }

    private:
        DisplayCurve();

        static constexpr std::size_t sBuckets = 4096;

        std::array<float, 256> mLinear;

        /// The least float that encodes to each byte, and past the last an infinity no value reaches; and
        /// the byte each of `sBuckets` even steps of the unit range starts at.
        std::array<float, 257> mLeast;
        std::array<std::uint8_t, sBuckets> mBucket;
    };

    /// `DisplayCurve::toLinear`, for a caller outside a loop.
    inline float toLinear(std::uint8_t encoded)
    {
        return DisplayCurve::get().toLinear(encoded);
    }

    /// The same over the three channels of a colour, which is how most of them arrive.
    osg::Vec3f toLinear(const osg::Vec3f& encoded);

    /// The byte nearest `value * 255`, held to nought and 255: `std::lround`'s answer for every finite
    /// value, a NaN nought, and no libm call, which no compiler inlines and a chain asks once a channel.
    /// In double, where the product and a half add exactly, so the cast rounds half away from nought as
    /// `std::lround` does. Held by two choices and no branch, so a loop of them is worked as a vector;
    /// asked as `>` and `<`, neither of which holds of a NaN, so one never reaches the cast.
    inline std::byte unitByte(float value)
    {
        const double scaled = static_cast<double>(value * 255.0f) + 0.5;
        const double held = scaled > 0.0 ? (scaled < 255.0 ? scaled : 255.0) : 0.0;
        return static_cast<std::byte>(static_cast<int>(held));
    }

    /// `DisplayCurve::encodedByte`, for a caller outside a loop.
    inline std::byte encodedByte(float linear)
    {
        return DisplayCurve::get().encodedByte(linear);
    }

    /// A colour as the content files store one, decoded — the one crossing, and every colour
    /// entering this renderer takes it: the game states four components and the trace holds three
    /// in light, so the narrowing and the decode are one step, and a particle's ramp cannot reach
    /// the sprite table in the space the file wrote it.
    osg::Vec3f decodeColour(std::uint32_t packed);

    /// The same decode, for a colour something else has already unpacked to `[0, 1]`. What the
    /// game hands over is display-encoded too: OpenMW's own renderer never converts. The alpha is
    /// dropped.
    osg::Vec3f decodeColour(const osg::Vec4f& encoded);

    /// The same again, for the four bytes `NifOsg` and `Terrain` write a vertex colour as.
    osg::Vec3f decodeColour(const osg::Vec4ub& encoded);

    /// And for what a content file said a surface is, which carries its space in its type.
    osg::Vec3f decodeColour(const EncodedColour& encoded);
}
