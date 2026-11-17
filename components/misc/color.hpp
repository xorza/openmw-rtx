#pragma once

#include <cstdint>
#include <string>

#include <osg/Vec4>

namespace Misc
{
    class Color
    {
        explicit Color(const osg::Vec4& value)
            : mValue(value)
        {
        }

    public:
        Color() = default;
        Color(float r, float g, float b, float a);

        float r() const { return mValue.r(); }
        float g() const { return mValue.g(); }
        float b() const { return mValue.b(); }
        float a() const { return mValue.a(); }

        std::string toString() const;

        static Color fromHex(std::string_view hex);
        static Color fromRGB(std::uint32_t value);
        static Color fromVec(osg::Vec4f value);

        std::string toHex() const;
        std::uint32_t toRGBA() const { return mValue.asRGBA(); }
        osg::Vec4f toVec() const { return mValue; }

        friend bool operator==(const Color& l, const Color& r);

    private:
        osg::Vec4 mValue;
    };
}
