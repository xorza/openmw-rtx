#include "specularalbedomaker.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/common/radicalinverse.hpp>
#include <components/rtx/shaders/brdf.h>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sSize = Shaders::SPECULAR_TABLE_SIZE;

        /// How many half vectors a node averages. Drawn from the normals the eye can see, so what is
        /// left of each term is `G2 / G1`, which is bounded by one: against the integral drawn with
        /// 2^20, the table is within 2.3e-4 at a cosine of 0.004 and 1.6e-4 elsewhere, where
        /// drawing from the distribution alone is eighteen times further off at that cosine.
        constexpr std::uint32_t sSamples = 4096;
    }

    std::vector<float> makeSpecularAlbedo()
    {
        // One Hammersley set for every node: the azimuth at stratum centres, turned into its cosine
        // and sine once, and the cap's height off the radical inverse.
        std::vector<osg::Vec2f> turned(sSamples);
        std::vector<float> raised(sSamples);
        for (std::uint32_t at = 0; at < sSamples; ++at)
        {
            const float azimuth = Shaders::TAU * (static_cast<float>(at) + 0.5f) / static_cast<float>(sSamples);
            turned[at] = osg::Vec2f(std::cos(azimuth), std::sin(azimuth));
            raised[at] = radicalInverse(at, 2);
        }

        std::vector<float> values(std::size_t{ sSize } * sSize * 2);
        for (std::uint32_t row = 0; row < sSize; ++row)
        {
            const float alpha = Shaders::ggxAlpha(Shaders::specularTableRoughness(row));

            for (std::uint32_t column = 0; column < sSize; ++column)
            {
                const float cosine = Shaders::specularTableCosine(column);
                const osg::Vec3f eye(std::sqrt(1.0f - cosine * cosine), 0.0f, cosine);

                double climbing = 0.0;
                double whole = 0.0;
                for (std::uint32_t at = 0; at < sSamples; ++at)
                {
                    // The whole cap: the bounded one integrates the same albedo, and the table
                    // is pinned to these draws.
                    const osg::Vec3f half = Shaders::visibleNormal(eye, alpha, raised[at], turned[at], 1.0f);

                    const float eyeHalf = eye * half;
                    const float lightCosine = 2.0f * eyeHalf * half.z() - cosine;
                    if (!(eyeHalf > 0.0f) || !(lightCosine > 0.0f))
                        continue;

                    const float weight = Shaders::smithShadowingGivenMasking(alpha, cosine, lightCosine);
                    climbing += static_cast<double>(weight * Shaders::schlickWeight(eyeHalf));
                    whole += static_cast<double>(weight);
                }

                const std::size_t node = (std::size_t{ row } * sSize + column) * 2;
                values[node] = static_cast<float>(climbing / sSamples);
                values[node + 1] = static_cast<float>(whole / sSamples);
            }
        }
        return values;
    }
}
