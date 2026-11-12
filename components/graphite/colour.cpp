#include "colour.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace Graphite
{
    namespace
    {
        using Linear = std::array<double, 3>;

        double decode(int channel)
        {
            const double encoded = channel / 255.0;
            return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
        }

        int encode(double linear)
        {
            const double clamped = std::clamp(linear, 0.0, 1.0);
            const double encoded
                = clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
            return static_cast<int>(std::lround(encoded * 255.0));
        }

        Linear linearOf(QRgb colour)
        {
            return { decode(qRed(colour)), decode(qGreen(colour)), decode(qBlue(colour)) };
        }

        QRgb opaqueOf(const Linear& linear)
        {
            return qRgb(encode(linear[0]), encode(linear[1]), encode(linear[2]));
        }

        /// Björn Ottosson's Oklab of linear sRGB (<https://bottosson.github.io/posts/oklab/>).
        std::array<double, 3> oklabOf(const Linear& c)
        {
            const double l = std::cbrt(0.4122214708 * c[0] + 0.5363325363 * c[1] + 0.0514459929 * c[2]);
            const double m = std::cbrt(0.2119034982 * c[0] + 0.6806995451 * c[1] + 0.1073969566 * c[2]);
            const double s = std::cbrt(0.0883024619 * c[0] + 0.2817188376 * c[1] + 0.6299787005 * c[2]);
            return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
        }

        Linear linearOfOklab(double lightness, double a, double b)
        {
            const double l = std::pow(lightness + 0.3963377774 * a + 0.2158037573 * b, 3.0);
            const double m = std::pow(lightness - 0.1055613458 * a - 0.0638541728 * b, 3.0);
            const double s = std::pow(lightness - 0.0894841775 * a - 1.2914855480 * b, 3.0);
            return { 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s };
        }

        /// Within sRGB to the precision the 8-bit grid rounds away.
        bool inGamut(const Linear& c)
        {
            constexpr double slack = 1e-9;
            return std::ranges::all_of(c, [](double channel) { return channel >= -slack && channel <= 1.0 + slack; });
        }

        /// What bisection halves an interval to: far under a step of the 8-bit grid.
        constexpr int sHalvings = 60;

        /// The colour at Oklab `lightness` and the hue of (`a`, `b`), at their chroma where sRGB holds
        /// it and at the most sRGB holds there where it does not.
        Linear atLightness(double lightness, double a, double b)
        {
            if (inGamut(linearOfOklab(lightness, a, b)))
                return linearOfOklab(lightness, a, b);
            double held = 0.0;
            double lost = 1.0;
            for (int at = 0; at < sHalvings; ++at)
            {
                const double scale = (held + lost) / 2.0;
                (inGamut(linearOfOklab(lightness, a * scale, b * scale)) ? held : lost) = scale;
            }
            return linearOfOklab(lightness, a * held, b * held);
        }

        double contrastOfLinear(const Linear& c, QRgb ground)
        {
            return contrast(opaqueOf(c), ground);
        }
    }

    double luminance(QRgb colour)
    {
        const Linear c = linearOf(colour);
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    }

    double contrast(QRgb first, QRgb second)
    {
        const double one = luminance(first);
        const double other = luminance(second);
        return (std::max(one, other) + 0.05) / (std::min(one, other) + 0.05);
    }

    QRgb over(QRgb ink, QRgb ground)
    {
        const int alpha = qAlpha(ink);
        const auto channel = [alpha](int from, int under) {
            return static_cast<int>(std::lround((from * alpha + under * (255 - alpha)) / 255.0));
        };
        return qRgb(
            channel(qRed(ink), qRed(ground)), channel(qGreen(ink), qGreen(ground)), channel(qBlue(ink), qBlue(ground)));
    }

    QRgb mixLinear(QRgb from, QRgb to, double amount)
    {
        const Linear one = linearOf(from);
        const Linear other = linearOf(to);
        return opaqueOf({ one[0] + (other[0] - one[0]) * amount, one[1] + (other[1] - one[1]) * amount,
            one[2] + (other[2] - one[2]) * amount });
    }

    QRgb liftedToClear(QRgb ink, QRgb ground, double floor)
    {
        if (contrast(ink, ground) >= floor)
            return ink;
        assert(luminance(ink) > luminance(ground) && "a lift for an ink darker than its ground");
        assert(contrast(qRgb(255, 255, 255), ground) >= floor && "a floor not even white clears");

        const std::array<double, 3> lab = oklabOf(linearOf(ink));
        double under = lab[0];
        double clears = 1.0;
        for (int at = 0; at < sHalvings; ++at)
        {
            const double lightness = (under + clears) / 2.0;
            (contrastOfLinear(atLightness(lightness, lab[1], lab[2]), ground) >= floor ? clears : under) = lightness;
        }

        // Rounding to the grid can land a hair under the floor: a step a grid's worth up at a time,
        // which white, clearing it, bounds.
        constexpr double sStep = 1.0 / 255.0;
        for (double lightness = clears; lightness <= 1.0 + sStep; lightness += sStep)
        {
            const QRgb rounded = opaqueOf(atLightness(std::min(lightness, 1.0), lab[1], lab[2]));
            if (contrast(rounded, ground) >= floor)
                return rounded;
        }
        return qRgb(255, 255, 255);
    }
}
