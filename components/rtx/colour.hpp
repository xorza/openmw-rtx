#pragma once

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

    /// sRGB's transfer function, and its inverse clamped to the unit range. Whatever is averaged is
    /// averaged between these two: half of one ground type and half of another meet at 188 in light
    /// and at 128 in bytes. One curve for everything the content hands over — a texel, a weather's
    /// colour, a lamp's. A value that is one of the 256 is answered from the table below whichever
    /// overload is called: the float one recovers the byte first, so the two answers agree.
    float toLinear(float encoded);
    float toEncoded(float linear);

    /// The same for a stored byte: the two hundred and fifty-six answers there are, worked out
    /// once, because a 512-square chain asks two million times and `std::pow` is a libm call.
    float toLinear(std::uint8_t encoded);

    /// The same over the three channels of a colour, which is how most of them arrive.
    osg::Vec3f toLinear(const osg::Vec3f& encoded);

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
