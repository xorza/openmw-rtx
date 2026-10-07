#include "skycontent.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <string_view>

#include <osg/Vec2f>

#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/sky.h>

#include "frameworld.hpp"

namespace Rtx
{
    namespace
    {
        /// Where a storm drives, as the pair a rotation about the zenith is written with: for a unit
        /// `(x, y)` measured from north the cosine and the sine are `y` and `x`. A direction nobody
        /// set is zero rather than north, so the shader is given north instead.
        osg::Vec2f bearingOf(const osg::Vec3f& storm)
        {
            const osg::Vec2f flat(storm.y(), storm.x());

            return flat.length2() > 0.0f ? flat : osg::Vec2f(1.0f, 0.0f);
        }

    }

    std::uint32_t SkyContent::sheetNamed(std::string_view name) const
    {
        if (name.empty())
            return sNoSheet;

        const auto found = std::find_if(
            mSheets.begin(), mSheets.end(), [&](const CloudSheet& sheet) { return sheet.mName == name; });
        return found != mSheets.end() ? static_cast<std::uint32_t>(found - mSheets.begin()) : sNoSheet;
    }

    const CloudSheet* SkyContent::drawable(std::uint32_t sheet) const
    {
        if (sheet >= mSheets.size() || mSheets[sheet].mTexture == sNoIndex)
            return nullptr;

        return &mSheets[sheet];
    }

    DeckLight deckLight(const Sun& sun, const osg::Vec3f& skyMean, std::span<const MoonPlacement, 2> moons)
    {
        // The sky's own radiance, less what the deck keeps of it. A layer under a hemisphere of
        // radiance `L` receives `pi L` and spreads what leaves its base over the hemisphere below,
        // so what comes back is `T * pi L / pi` — the `pi` divides out and a deck is simply a
        // fraction of the sky it hides.
        const osg::Vec3f fromSky = skyMean * Shaders::CLOUD_TRANSMISSION;

        // A direction has to be turned into a level surface's share of it first. The layer is
        // flat and the light is not overhead, so what lands is `E cos`, and what leaves the base is
        // that spread over the lower hemisphere.
        const auto sentDown = [](const osg::Vec3f& irradiance, const osg::Vec3f& towards) {
            return irradiance * (std::max(towards.z(), 0.0f) * Shaders::INV_PI * Shaders::CLOUD_TRANSMISSION);
        };

        osg::Vec3f direct = sentDown(sun.mIrradiance, sun.mPosition);
        for (const MoonPlacement& moon : moons)
            direct += sentDown(moon.getPaintedIrradiance(), moon.mDirection);

        return DeckLight{ .mLit = fromSky + direct, .mShadowed = fromSky };
    }

    Shaders::CloudDeck describeClouds(const CloudCrossing& clouds, const DeckLight& light, const SkyContent& textures)
    {
        const float blend = clouds.mBlend;

        // Cleaned where it enters, by the reader that takes it off the weather (`SkyReader::read`):
        // the builder is handed a share and never a content file's division.
        assert(blend >= 0.0f && blend <= 1.0f && "a cloud blend the reader did not clean");

        // **The four cases the rasterizer's two meshes make**: the near sheet at `1 - blend` and the
        // one ahead at `blend`. Both, and they cross. The near one alone, and it stands at both ends
        // of the blend on its own bearing, its mean the one it is read against. The one ahead
        // alone — out of ash into clear — and it fades in by the blend, standing at both ends on
        // its bearing. Neither, and there is no deck; nor is there where the mesh gave up no shape.
        const CloudSheet* const near = textures.drawable(clouds.mSheet);
        const CloudSheet* const ahead = textures.drawable(clouds.mNext);
        const CloudSheet* const first = near != nullptr ? near : ahead;
        const CloudSheet* const second = ahead != nullptr ? ahead : near;
        const bool crosses = near != nullptr && ahead != nullptr;
        const bool shaped = textures.mShell.mTiles.x() > 0.0f;
        const auto crossing = [&](float from, float to) { return crosses ? from * (1.0f - blend) + to * blend : from; };

        const osg::Vec3f& storm = near != nullptr ? clouds.mDirection : clouds.mNextDirection;
        const osg::Vec3f& stormAhead = ahead != nullptr ? clouds.mNextDirection : clouds.mDirection;
        const float opacity = first == nullptr || !shaped ? 0.0f : near != nullptr ? 1.0f : blend;

        return Shaders::CloudDeck{
            .mOpacity = opacity,

            .mLit = light.mLit,
            .mShadowed = light.mShadowed,
            .mMean = first != nullptr ? crossing(first->mMean, second->mMean) : 0.0f,

            // A world height and a tile's own width, which is what anchors the sheet to the
            // ground under it rather than to the eye. `Rtx::sCloudAltitude` is the chosen number and
            // the mesh's own height in tiles is what turns it into a width.
            .mAltitude = sCloudAltitude,
            .mPerTile = textures.mShell.mTiles / sCloudAltitude,

            // The shader mixes the two by this unconditionally; where one sheet stands at both
            // ends the mix is that sheet.
            .mBlend = blend,
            .mScroll = clouds.mScroll,

            // Turned to face where each weather is driving, which is what the engine does to
            // each of its two cloud meshes: the deck of an ashstorm runs the way the ash does. A
            // weather with nothing to drive leaves the direction due north, and this due north too.
            .mBearing = bearingOf(storm),
            .mNextBearing = bearingOf(stormAhead),

            .mCurvature = textures.mShell.mCurvature,
            .mRings = textures.mShell.mRings,

            .mTexture = first != nullptr ? static_cast<std::uint32_t>(first->mTexture) : Shaders::NO_TEXTURE,
            .mNext = second != nullptr ? static_cast<std::uint32_t>(second->mTexture) : Shaders::NO_TEXTURE,
        };
    }

    Shaders::StarField describeStars(float fade, float glare, float turn, const SkyContent& textures)
    {
        const float seen = fade * glare;

        const bool drawn = seen > 0.0f && textures.mNight.mField != sNoIndex && textures.mNight.mTile > 0.0f;

        return Shaders::StarField{
            .mFade = seen,
            .mGlow = textures.mNight.mGlow * seen,
            .mTurn = turn,
            .mTile = textures.mNight.mTile,
            .mHorizon = textures.mNight.mHorizon,

            // A sheet nobody can see is one nothing has to sample, and saying so here is what keeps
            // the test out of the shader's hot path.
            .mTexture = drawn ? static_cast<std::uint32_t>(textures.mNight.mField) : Shaders::NO_TEXTURE,
        };
    }

    void describePatches(
        float turn, const SkyContent& textures, std::span<Shaders::SkyPatch, Shaders::SKY_PATCH_COUNT> patches)
    {
        // Straight up with no texture, which is a patch the sky skips — and what an interior and a
        // mesh with fewer than six of them both leave behind.
        const Shaders::SkyPatch none = noPatch();

        for (std::size_t patch = 0; patch < patches.size(); ++patch)
        {
            const NightSky::Patch& placed = textures.mNight.mPatches[patch];
            if (placed.mTexture == sNoIndex || !(placed.mAngularRadius > 0.0f))
            {
                patches[patch] = none;
                continue;
            }

            // Turned with the star sphere, because that is the mesh they are painted on: a rotation
            // about the zenith, which is what the engine gives the whole night node.
            const osg::Vec3f towards(placed.mDirection.x() * std::cos(turn) - placed.mDirection.y() * std::sin(turn),
                placed.mDirection.x() * std::sin(turn) + placed.mDirection.y() * std::cos(turn), placed.mDirection.z());

            // A canonical orientation, because the mesh's own is not recoverable from a centre and
            // a radius. What a patch is painted with is a soft wash or a scatter of stars, neither
            // of which reads as turned the wrong way; keeping `mUp` as near the zenith as the patch
            // allows is what stops one drifting as the sphere rolls.
            osg::Vec3f up = osg::Vec3f(0.0f, 0.0f, 1.0f) - towards * towards.z();
            if (up.length2() < 1.0e-6f)
                up = osg::Vec3f(0.0f, 1.0f, 0.0f);
            up.normalize();

            osg::Vec3f right = up ^ towards;
            right.normalize();

            patches[patch] = Shaders::SkyPatch{ .mDirection = towards,
                .mRight = right,
                .mUp = up,
                .mLimb = std::sin(placed.mAngularRadius),
                .mTexture = static_cast<std::uint32_t>(placed.mTexture) };
        }
    }
}
