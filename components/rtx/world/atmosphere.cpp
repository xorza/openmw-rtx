#include "atmosphere.hpp"
#include <algorithm>
#include <cmath>

namespace Rtx
{
    namespace
    {
        /// The antiderivative of `sin e cos e · Q'(e) / Q(e)` for `Q = C cos e - D sin e`, written
        /// through `Q = R cos(e + φ)`: `-cos 2φ · e / 2 + sin 2e / 4 + sin 2φ · ln(Q / R) / 2`.
        double tiltedLog(double e, double c, double d)
        {
            const double r2 = c * c + d * d;
            const double q = c * std::cos(e) - d * std::sin(e);
            return -0.5 * (c * c - d * d) / r2 * e + 0.25 * std::sin(2.0 * e)
                + (c * d) / r2 * std::log(q / std::sqrt(r2));
        }
    }

    float zenithShareOf(const Shaders::SkyRamp& ramp)
    {
        // The share is `t = (A s - B c) / (C c - D s)` between the rings, `s = sin e`, `c = cos e`,
        // and the cosine-weighted mean is `2 ∫ z t dz = 2 ∫ s c t de`. Writing the numerator as
        // `α Q + β Q'` over the denominator `Q` makes `t = α + β Q' / Q`, whose two terms integrate
        // to `α s² / 2` and `β · tiltedLog`.
        const double a = ramp.mLow.x();
        const double b = ramp.mLow.y();
        const double d = ramp.mStep.x();
        const double c = ramp.mStep.y();
        const double r2 = c * c + d * d;
        const double alpha = (-a * d - b * c) / r2;
        const double beta = (b * d - a * c) / r2;

        // Below the horizon is no part of the sky a surface facing it receives.
        const double bottom = std::clamp(double{ ramp.mBottom }, 0.0, 1.0);
        const double top = std::clamp(double{ ramp.mTop }, bottom, 1.0);
        const double from = std::asin(bottom);
        const double to = std::asin(top);

        const double wall
            = alpha * (top * top - bottom * bottom) + 2.0 * beta * (tiltedLog(to, c, d) - tiltedLog(from, c, d));
        return float(1.0 - top * top + wall);
    }
}
