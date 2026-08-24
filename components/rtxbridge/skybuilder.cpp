#include "skybuilder.hpp"

#include <cmath>
#include <string>

#include <components/misc/strings/lower.hpp>
#include <components/sky/clouds.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "lightbuilder.hpp"

namespace RtxBridge
{
    std::uint32_t SkyTextures::cloudsOf(std::uint32_t weather) const
    {
        if (weather >= mClouds.size() || mClouds[weather] == Rtx::sNoIndex)
            return Rtx::Shaders::NO_SKY_TEXTURE;

        return static_cast<std::uint32_t>(mClouds[weather]);
    }

    SkyTextures addSkyTextures(Rtx::SceneDesc& scene, const VFS::Manager& vfs)
    {
        const auto hold = [&](const std::string& name) {
            // The file records a bare name and the archive holds it under `textures/`, which is the
            // same join the scene manager makes before it is handed one.
            const VFS::Path::Normalized path("textures/" + Misc::StringUtils::lowerCase(name));
            if (!vfs.exists(path))
                return Rtx::sNoIndex;

            const Rtx::Index slot = scene.addTexture(path);
            scene.holdTexture(slot);
            return slot;
        };

        SkyTextures loaded;
        loaded.mClouds.fill(Rtx::sNoIndex);

        for (std::uint32_t weather = 0; weather < Rtx::Shaders::WEATHER_COUNT; ++weather)
        {
            const std::string_view named = Sky::cloudTexture(weatherName(weather));
            if (!named.empty())
                loaded.mClouds[weather] = hold(std::string(named));
        }

        loaded.mStars = hold(std::string(Sky::starSheet()).substr(std::string("textures/").size()));

        return loaded;
    }

    void dropSkyTextures(Rtx::SceneDesc& scene, const SkyTextures& textures)
    {
        for (const Rtx::Index slot : textures.mClouds)
            if (slot != Rtx::sNoIndex)
                scene.dropTexture(slot);

        scene.dropTexture(textures.mStars);
    }

    Rtx::Shaders::CloudDeck describeClouds(std::uint32_t weather, std::uint32_t next, float blend,
        const osg::Vec4f& fog, const osg::Vec3f& storm, float scroll, const SkyTextures& textures)
    {
        const std::uint32_t slot = textures.cloudsOf(weather);

        // **Written so a NaN lands on nought, which `std::clamp` does not do.** The blend comes off a
        // content file by way of a division, and a content file is untrusted: `clamp` compares and
        // hands back what it was given when both comparisons fail, so a NaN goes straight through it
        // and on into a `mix` that blacks out the sky. Asking whether it is inside the range instead
        // of whether it is outside is the whole of the difference.
        const float mixed = blend > 0.0f ? (blend < 1.0f ? blend : 1.0f) : 0.0f;

        return Rtx::Shaders::CloudDeck{
            // A weather whose deck was never loaded has no deck, which is the same thing an interior
            // has and is said the same way.
            .mOpacity = slot == Rtx::Shaders::NO_SKY_TEXTURE ? 0.0f : 1.0f,

            .mColour = decodeColour(Sky::cloudColour(fog)),
            .mBlend = mixed,
            .mScroll = scroll,

            // **Turned to face where the weather is driving**, which is what the engine does to the
            // whole cloud mesh: the deck of an ashstorm runs the way the ash does. A weather with
            // nothing to drive leaves the direction due north, and this at nought.
            .mTurn = std::atan2(storm.x(), storm.y()),

            .mTexture = slot,
            .mNext = textures.cloudsOf(next),
        };
    }

    Rtx::Shaders::StarField describeStars(float fade, float glare, float turn, const SkyTextures& textures)
    {
        const float seen = fade * glare;

        return Rtx::Shaders::StarField{
            .mFade = seen,
            .mTurn = turn,

            // A sheet nobody can see is one nothing has to sample, and saying so here is what keeps
            // the test out of the shader's hot path.
            .mTexture = seen > 0.0f ? static_cast<std::uint32_t>(textures.mStars) : Rtx::Shaders::NO_SKY_TEXTURE,
        };
    }
}
