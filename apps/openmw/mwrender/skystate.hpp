#pragma once

#include <osg/Vec3f>

#include <components/sky/moonstate.hpp>
#include <components/sky/timeofday.hpp>
#include <components/vfs/pathutil.hpp>

#include "skyutil.hpp"

namespace MWRender
{
    /// What the weather manager settled about the sky, in the world's own numbers, undecoded:
    /// every colour is a content file's three bytes over 255, and what that means is a question
    /// about a renderer's transport. `MWWorld::WeatherManager` owns one and writes it where it
    /// decides each part; both renderers reach it through `MWBase::World::getSkyState` and derive
    /// what they draw from it when they draw. Nothing here is recorded a second time by a setter.
    /// What the game itself decides — the toggles, the water, the sun light — is `WorldState`'s.
    struct SkyState
    {
        /// The weather the world settled on, whole, as `WeatherManager::calculateWeatherResult`
        /// returned it, its names standing on `mNames` (`keepNames`).
        WeatherResult mWeather;

        /// The four names `mWeather`'s views stand on: copies of the weather records' own, made
        /// where a name changed. **Owned and not the records'**, because the renderers read this
        /// while the Lua worker runs, and a global script that sets a record's path frees the string
        /// a view of it stood on.
        struct Names
        {
            VFS::Path::Normalized mCloudTexture;
            VFS::Path::Normalized mNextCloudTexture;
            VFS::Path::Normalized mParticleEffect;
            VFS::Path::Normalized mRainEffect;
        };
        Names mNames;

        /// Stands each of `mWeather`'s names on its copy in `mNames`, copied only where the name
        /// changed: a weather's names change when the weather does, and not on a frame.
        void keepNames()
        {
            keepName(mWeather.mCloudTexture, mNames.mCloudTexture);
            keepName(mWeather.mNextCloudTexture, mNames.mNextCloudTexture);
            keepName(mWeather.mParticleEffect, mNames.mParticleEffect);
            keepName(mWeather.mRainEffect, mNames.mRainEffect);
        }

        /// The hours the day is divided into, which the sun's disc and the stars ramp by.
        Sky::TimeOfDaySettings mTimes;

        /// Whether the weather ran this update, which is what says everything below is current: it
        /// runs outdoors, and indoors it stops at once and the rest holds whatever it last held.
        /// Not where the player stands, which is `WorldState::mLocation`, read at the frame.
        bool mWeatherRan = false;

        /// The orbit's direction, as `WeatherManager::update` runs the sun east to west. Where the
        /// disc is drawn is `Sky::sunDiscPosition` of it, and where the light comes from is each
        /// renderer's own answer (`match sunlight to sun`).
        osg::Vec3f mSunDirection;
        bool mNight = false;

        /// Whether the disc is drawn at this hour, `Sky::sunUp`: the weather manager hides it
        /// through the night.
        bool mSunUp = true;

        /// How far the glare has come up since sunrise, nought to one over the day.
        float mGlareFade = 1.0f;

        /// Masser and Secunda. An alpha of nothing is a moon that is not drawn.
        Sky::MoonState mMoons[2] = {};

        /// What drives the particle effect: off Red Mountain at the player in an ash or blight
        /// storm, and due north otherwise. Not the deck's direction, which is the weather's own.
        /// Read only under a storm, which the same update that writes this declares.
        osg::Vec3f mStormParticleDirection;

    private:
        static void keepName(VFS::Path::NormalizedView& view, VFS::Path::Normalized& owned)
        {
            if (owned != view)
                owned = VFS::Path::Normalized(view);
            view = owned;
        }
    };
}
