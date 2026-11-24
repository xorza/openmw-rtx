#include "skyreader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>

#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <apps/openmw/mwrender/sceneframe.hpp>
#include <apps/openmw/mwrender/sky.hpp>
#include <apps/openmw/mwrender/skystate.hpp>
#include <apps/openmw/mwrender/skyutil.hpp>
#include <components/fallback/fallback.hpp>
#include <components/misc/constants.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/environment/moonfaces.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/world/fogbuilder.hpp>
#include <components/rtx/world/skylight.hpp>
#include <components/rtx/world/sunglare.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/settings/values.hpp>
#include <components/sky/moonstate.hpp>
#include <components/sky/skyclock.hpp>
#include <components/sky/sundisc.hpp>
#include <components/sky/timeofday.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender
{
    namespace
    {
        osg::Vec3f waterScatter()
        {
            // The content's water colour at its weight, and not the air's share `FogManager` mixes in: read
            // as the water's albedo that share would turn a clear noon's sea a muddy grey and undo the
            // pairing `look.h` states with `WATER_EXTINCTION`.
            const osg::Vec4f colour = Fallback::Map::getColour("Water_UnderwaterColor");
            return osg::Vec3f(colour.r(), colour.g(), colour.b())
                * Fallback::Map::getFloat("Water_UnderwaterColorWeight");
        }

        float smoothstep01(float x)
        {
            const float t = std::clamp(x, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /// Smooth dawn-to-day-to-dusk envelope extending across Morrowind's exterior sky/fog twilight
        /// windows so interior window portal light and emissive glass fade smoothly into night and out
        /// of dawn without snapping at `mSunriseTime` or `mNightStart`.
        float interiorTwilightFactor(const float hour, const Sky::TimeOfDaySettings& times)
        {
            const float daySpan = times.mNightStart < times.mNightEnd ? (times.mNightStart + 24.0f - times.mNightEnd)
                                                                      : (times.mNightStart - times.mNightEnd);
            if (!(daySpan > 0.0f) || daySpan >= 24.0f)
                return 1.0f;

            const Sky::WeatherSetting skySetting = times.getSetting("Sky");
            const Sky::WeatherSetting fogSetting = times.getSetting("Fog");
            const float preDawn = std::max(0.25f, std::max(fogSetting.mPreSunriseTime, skySetting.mPreSunriseTime));
            const float postDusk = std::max(0.50f, std::max(fogSetting.mPostSunsetTime, skySetting.mPostSunsetTime));
            const float sunriseDuration = std::max(times.mDayStart - times.mNightEnd, times.mSunriseDuration);
            const float sunsetDuration = std::max(times.mNightStart - times.mDayEnd, 0.0f);

            const float dawnStart = times.mNightEnd - preDawn;
            float h = std::fmod(hour - dawnStart, 24.0f);
            if (h < 0.0f)
                h += 24.0f;

            const float dawnSpan = preDawn + std::max(sunriseDuration * 0.5f, 0.5f);
            const float duskEndOffset = preDawn + daySpan + postDusk;
            const float duskFadeOffset = std::max(dawnSpan, preDawn + daySpan - sunsetDuration * 0.5f);

            if (h >= duskEndOffset)
                return 0.0f;
            if (h < dawnSpan)
                return smoothstep01(h / std::max(dawnSpan, 1e-4f));
            if (h > duskFadeOffset)
                return smoothstep01((duskEndOffset - h) / std::max(duskEndOffset - duskFadeOffset, 1e-4f));
            return 1.0f;
        }

        struct ComputedInteriorWindowTerms
        {
            Rtx::Sun mSun;
            osg::Vec3f mSunDirection{ 0.0f, 0.0f, 1.0f };
            osg::Vec3f mSkyHorizon;
            osg::Vec3f mSkyZenith;
            osg::Vec3f mSkyMean;
            float mTwilight = 1.0f;
        };

        ComputedInteriorWindowTerms computeInteriorWindowTerms(
            const SkyState& sky, const WorldState& world, float zenithShare, float sunlightStrength)
        {
            const WeatherResult& weather = sky.mWeather;

            osg::Vec3f exteriorSunDir = Sky::sunDiscPosition(sky.mSunDirection);
            if (exteriorSunDir.length2() > 0.0f)
                exteriorSunDir.normalize();
            else
                exteriorSunDir = osg::Vec3f(0.0f, 0.0f, 1.0f);

            const osg::Quat toInterior(-world.mNorthMarkerYaw, osg::Vec3f(0.0f, 0.0f, 1.0f));
            osg::Vec3f interiorSunDir = toInterior * exteriorSunDir;
            if (interiorSunDir.length2() > 0.0f)
                interiorSunDir.normalize();

            const float smoothSunShare = smoothstep01(Rtx::sunShareAt(world.mGameHour, sky.mTimes));
            const Rtx::SkyReading exteriorReading{
                .mSunPosition = exteriorSunDir,
                .mSunShare = smoothSunShare,
                .mSunShareAloft = Rtx::sunShareAloft(world.mGameHour, sky.mTimes),
                .mSunColour = Rtx::decodeColour(weather.mSunColor),
                .mAmbient = Rtx::decodeColour(weather.mAmbientColor),
                .mDiscColour = Rtx::decodeColour(weather.mSunDiscColor),
                .mGlare = weather.mGlareView,
            };
            const Rtx::Skylight exteriorLight = Rtx::makeSkylight(exteriorReading);
            Rtx::Sun interiorSun = exteriorLight.mSun;
            interiorSun.mPosition = interiorSunDir;

            // Keep low-elevation sunrise/sunset shafts warm amber via Rayleigh transmittance rather
            // than allowing Morrowind's late-sunset `Sun_Night_Color` blue bleed to chill the beam.
            const osg::Vec3f atmo = Rtx::airTransmittance(std::max(exteriorSunDir.z(), 0.0f));
            const float atmoMax = std::max(atmo.x(), 1e-4f);
            const osg::Vec3f atmoHue = atmo / atmoMax;
            const float warmthMix = 0.65f * (1.0f - smoothSunShare);
            const osg::Vec3f horizonTint(1.0f + (atmoHue.x() - 1.0f) * warmthMix,
                1.0f + (atmoHue.y() - 1.0f) * warmthMix, 1.0f + (atmoHue.z() - 1.0f) * warmthMix);
            interiorSun.mIrradiance = osg::componentMultiply(interiorSun.mIrradiance, horizonTint);

            interiorSun.mIrradiance *= std::clamp(weather.mGlareView, 0.0f, 1.0f) * sunlightStrength;
            if (!sky.mSunUp)
                interiorSun.mIrradiance = osg::Vec3f(0.0f, 0.0f, 0.0f);

            const osg::Vec3f rawHorizon = Rtx::decodeColour(weather.mFogColor);
            const osg::Vec3f rawZenith = Rtx::decodeColour(weather.mSkyColor);
            const Rtx::SkyBudget exteriorBudget
                = Rtx::skyBudget(rawHorizon, rawZenith, zenithShare, osg::Vec3f(), exteriorLight.mAmbient);
            const float twilight = interiorTwilightFactor(world.mGameHour, sky.mTimes);
            const osg::Vec3f fill = exteriorBudget.mFill * twilight;

            return ComputedInteriorWindowTerms{
                .mSun = interiorSun,
                .mSunDirection = interiorSunDir,
                .mSkyHorizon = rawHorizon + fill,
                .mSkyZenith = rawZenith + fill,
                .mSkyMean = rawHorizon * 0.65f + rawZenith * 0.35f + fill,
                .mTwilight = twilight,
            };
        }
    }

    SkyReader::SkyReader()
        : mMoonPaint(Rtx::decodeColour(Fallback::Map::getColour("Moons_Script_Color")))
        , mGlare(Sky::SunGlareFader::read())
        , mMoonSizes{ Fallback::Map::getFloat("Moons_Masser_Size"), Fallback::Map::getFloat("Moons_Secunda_Size") }
        , mWaterScatter(waterScatter())
    {
    }

    Rtx::SkySources SkyReader::sources()
    {
        Rtx::SkySources sources{
            .mClouds = Settings::models().mSkyclouds,
            .mAtmosphere = Settings::models().mSkyatmosphere,
            .mStars = Settings::models().mSkynight02,
            .mStarsFallback = Settings::models().mSkynight01,
            .mCloudSheets = {},
        };
        for (std::size_t weather = 0; weather < Rtx::sWeatherCount; ++weather)
            sources.mCloudSheets[weather] = Fallback::Map::getString(
                std::format("Weather_{}_Cloud_Texture", Rtx::nameOf(static_cast<Rtx::Weather>(weather))));
        return sources;
    }

    void SkyReader::listAssets(const VFS::Manager& vfs, std::vector<VFS::Path::Normalized>& models,
        std::vector<VFS::Path::Normalized>& textures)
    {
        const Rtx::SkySources sky = sources();
        models.push_back(sky.mClouds);
        models.push_back(sky.mAtmosphere);
        if (vfs.exists(sky.mStars))
            models.push_back(sky.mStars);
        models.push_back(sky.mStarsFallback);

        for (const Rtx::Moon moon : { Rtx::Moon::Masser, Rtx::Moon::Secunda })
            textures.emplace_back(Rtx::moonFaceOf(moon));
    }

    void SkyReader::attach(Rtx::SceneDesc& scene, Resource::SceneManager& scenes, Rtx::ThreadContent& thread)
    {
        mMoonFaces = Rtx::addMoonFaces(scene, *scenes.getImageManager(), mMoonSizes, thread, mHolds);
        mSkyContent = Rtx::addSkyContent(scene, scenes, sources(), thread, mHolds);
        mTimescaleClouds = Fallback::Map::getBool("Weather_Timescale_Clouds");
    }

    void SkyReader::follow(
        const SkyState& sky, Rtx::SceneDesc& scene, Resource::SceneManager& scenes, Rtx::ThreadContent& thread)
    {
        for (const VFS::Path::NormalizedView name : { sky.mWeather.mCloudTexture, sky.mWeather.mNextCloudTexture })
            if (!name.empty() && mSkyContent.sheetNamed(name.value()) == Rtx::sNoSheet)
                Rtx::addCloudSheet(scene, scenes, thread, mHolds, name.value(), mSkyContent);
    }

    void SkyReader::detach(Rtx::SceneDesc& scene)
    {
        scene.drop(mHolds);
        mSkyContent = Rtx::SkyContent{};
        mMoonFaces = Rtx::MoonFaces{};
    }

    Rtx::WorldReading SkyReader::read(const SkyState& sky, const WorldState& world, const Precipitation& falling,
        const double seconds, const float reach, const bool hasWindowPortals) const
    {
        const WeatherResult& weather = sky.mWeather;

        // Where the sun is drawn, and the light comes back along it. Not the light's own position,
        // which `match sunlight to sun` may have left on the orbit; nothing that traces can hold both.
        const osg::Vec4f disc = sunDiscOf(sky, world);
        osg::Vec3f discAt(disc.x(), disc.y(), disc.z());
        if (discAt.length2() > 0.0f)
            discAt.normalize();

        // A room's light out of the record the cell wrote and not the rasterizer's reading of it,
        // which lifts the ambient for its own falloff and points a directional light.
        const std::optional<Rtx::Daylight> room
            = world.mRoom.has_value() ? std::optional(Rtx::makeRoomLight(*world.mRoom)) : std::nullopt;

        // The horizon is the fog and the zenith is the sky's own, which is the pair Morrowind
        // records. Decoded here, because the world does not know what a transport is.
        const osg::Vec3f haze = room.has_value() ? room->mSkyHorizon : Rtx::decodeColour(world.mAir.mColour);

        // Whether there is a sky to draw, as the game decides it (`WorldState::mSkyShown`). Off, the
        // rasterizer hides the sky node whole and clears to the fog colour, while the sun and weather go
        // on lighting.
        const bool skyShown = world.mSkyShown;

        // An interior has no sky colour, as the weather system stops writing it indoors, so the air's own
        // stands in. A sky turned off keeps it: what the sky lights is unchanged, and what an eye sees there
        // is the frame's to say (`Rtx::WorldReading::mSkyDrawn`).
        const osg::Vec3f zenith = room.has_value() ? room->mSkyZenith : Rtx::decodeColour(weather.mSkyColor);

        // The sun is not assembled here: everything the world says about it goes to the one builder
        // that decides what a sun may be, and the light is taken whole from whichever built it.
        const Rtx::SkyReading reading{
            .mSunPosition = discAt,

            // Off the hour rather than off the disc's alpha, which the rasterizer leaves at one all
            // night with the disc hidden.
            .mSunShare = Rtx::sunShareAt(world.mGameHour, sky.mTimes),
            .mSunShareAloft = Rtx::sunShareAloft(world.mGameHour, sky.mTimes),
            .mSunColour = Rtx::decodeColour(world.mSunColour),
            .mAmbient = Rtx::decodeColour(world.mAmbientBeforeNightEye),
            .mDiscColour = Rtx::decodeColour(weather.mSunDiscColor),
            .mGlare = weather.mGlareView,
        };
        const Rtx::Skylight light = room.has_value() ? room->mLight : Rtx::makeSkylight(reading);

        // The recorded depth and not the ramp `FogManager` made of it, which exists to hide a far clip plane.
        // A quasi-exterior stands in the weather's air, as handed to `roomFog` it closed over the sky. The
        // base wind and not the gust: the fog's depth and drift are the weather's settled character.
        const auto openAir = world.mLocation == Location::Exterior ? &Rtx::exteriorFog : &Rtx::quasiExteriorFog;
        const Rtx::Fog air
            = room.has_value() ? room->mFog : openAir(haze, weather.mFogDepth, weather.mBaseWindSpeed, reach);

        // Before the frame rather than into it, because the deck is lit by them (`Rtx::deckLight`).
        std::array<Rtx::MoonPlacement, 2> moons{};
        for (std::size_t moon = 0; moon < moons.size(); ++moon)
        {
            const Sky::MoonState& state = sky.mMoons[moon];

            // The glare is applied here, where the rasterizer applies it too
            // (`SkyManager::setWeather` calls `Moon::adjustTransparency` after the hand-over).
            moons[moon] = Rtx::placeMoon(mMoonFaces, static_cast<Rtx::Moon>(moon), state.mRotationFromHorizon,
                state.mRotationFromNorth, state.mPhaseEighths, state.mDaylightFade * weather.mGlareView);
        }

        // Secunda alone, as `SkyManager::setMoonColour` paints it.
        if (world.mMoonRed)
            moons[static_cast<std::size_t>(Rtx::Moon::Secunda)].mPaint = mMoonPaint;

        // Nothing recorded is not a rate: `MWWorld::Weather::cloudBlendFactor` divides by
        // `Clouds_Maximum_Percent`. The fallbacks' nought for ash and blight no longer reaches it
        // (`esmfallbacks.lua` leaves the record's default standing), but a share past one, an infinity or a
        // NaN would still mix the tracer's whole sky. Nothing recorded means the deck has crossed at once.
        const float cloudBlend
            = std::isfinite(weather.mCloudBlendFactor) ? std::clamp(weather.mCloudBlendFactor, 0.f, 1.f) : 1.f;

        std::optional<Rtx::InteriorWindowSky> interiorWindow;
        if (!world.isOutdoors() && room.has_value() && sky.mExteriorValid && mInteriorWindowSunlight && hasWindowPortals)
        {
            const ComputedInteriorWindowTerms terms = computeInteriorWindowTerms(
                sky, world, mSkyContent.mAtmosphere.mZenithShare, mInteriorSunlightStrength);
            // Cap directly-seen window glass brightness gently so cranking skylight/sunlight up
            // does not slam auto-exposure down and crush indirect light inside the room, and blend
            // smoothly down to a dim night-sky baseline during dusk and dawn.
            const float dayGlassGain
                = std::min(1.0f + 0.35f * std::sqrt(std::max(mInteriorWindowSkylight, 0.0f)), 2.8f);
            const float glassGain = 0.25f + (dayGlassGain - 0.25f) * terms.mTwilight;
            interiorWindow = Rtx::InteriorWindowSky{
                .mSun = terms.mSun,
                .mSkyHorizon = terms.mSkyHorizon * glassGain,
                .mSkyZenith = terms.mSkyZenith * glassGain,
                .mDust = Rtx::Shaders::INTERIOR_DUST_EXTINCTION * mInteriorWindowGodrays,
            };
        }

        return Rtx::WorldReading{
            .mDaylight = Rtx::Daylight{
                .mLight = light,
                .mSkyHorizon = haze,
                .mSkyZenith = zenith,

                // The engine's four-point `Stars` ramp at this hour, before the weather's glare is
                // taken off it, and nothing by day.
                .mStarFade = weather.mNight ? weather.mNightFade : 0.f,
                .mFog = air,
            },
            .mOutdoors = world.isOutdoors(),
            .mSkyDrawn = skyShown,
            .mGlare = weather.mGlareView,
            .mStarRoll = Sky::starRoll(world.mGameTime),
            .mMoons = moons,
            .mClouds = Rtx::CloudCrossing{
                // The sheets the weather names, as the rasterizer is handed them. The one ahead
                // only while a weather is arriving: outside a transition the engine leaves the last
                // one's name standing at a blend of nothing.
                .mSheet = mSkyContent.sheetNamed(weather.mCloudTexture.value()),
                .mNext = world.mNextWeatherId.has_value() ? mSkyContent.sheetNamed(weather.mNextCloudTexture.value())
                                                          : Rtx::sNoSheet,
                .mBlend = cloudBlend,
                // Reported rather than derived, because an ash or blight storm blows off Red
                // Mountain at the player; one each, because the rasterizer turns each of its two
                // cloud meshes by its own weather's storm.
                .mDirection = weather.mStormDirection,
                .mNextDirection = weather.mNextStormDirection,
                .mScroll = mClock.mCloudScroll,
            },

            // Negative infinity and not zero: zero is sea level, and a cell with no water has to
            // answer "how deep is this point" with never.
            .mWaterLevel = world.mWater.isShown() ? world.mWater.mHeight : -std::numeric_limits<float>::infinity(),

            .mWaterScatter = mWaterScatter,

            // What the sea is animated by, in elapsed seconds rather than frames, or the sea would
            // slow down whenever the frame did.
            .mSeconds = seconds,
            .mSkySeconds = mClock.mSeconds,

            .mRainOnWater = falling.getRainOnWater(),

            // The top of the box the rasterizer's `PrecipitationOccluder::update` draws its depth
            // map from: the precipitation's own range and a cell over it, above the eye. Nought
            // where `weather particle occlusion` is off, or where the game says what is falling is
            // not the kind a roof stops — ash and blight blow under one, and rain and snow do not.
            .mShelterHeight
            = falling.isOccluded() ? falling.getOcclusionRange().z() + Constants::CellSizeInUnits : 0.0f,

            // The fader's strength as `SunGlareCallback` multiplies it up: `_Max` by the time-of-day fade by
            // the weather's `Glare_View` by the disc's sunrise and sunset alpha. The glare hangs under the
            // sun's transform, so a hidden sun or a sky `tsky` turned off draws none.
            .mSunGlare = Rtx::SunGlare{
                .mFader = mGlare,
                .mFade = skyShown && sky.mSunUp ? sky.mGlareFade * weather.mGlareView * weather.mSunDiscColor.a() : 0.0f,
            },

            // **Out of the world's light, both indoors and out**: the room's fill and the sky's
            // ambient are read without it, and the display chain lays it over the picture.
            .mNightEye = osg::Vec3f(world.mNightEye.x(), world.mNightEye.y(), world.mNightEye.z()),
            .mInteriorWindowSky = interiorWindow,
        };
    }

    WindowPortalLighting SkyReader::readWindowPortalLighting(
        const WorldState& world, const SkyState& sky, const bool hasWindowPortals) const
    {
        if (world.isOutdoors() || !world.mRoom.has_value() || !sky.mExteriorValid || !mInteriorWindowSunlight
            || !hasWindowPortals)
            return {};

        const ComputedInteriorWindowTerms terms = computeInteriorWindowTerms(
            sky, world, mSkyContent.mAtmosphere.mZenithShare, mInteriorSunlightStrength);
        return WindowPortalLighting{
            .mActive = true,
            .mSkyScatter = terms.mSkyMean * (6.0f * terms.mTwilight * mInteriorWindowSkylight),
            .mSunDirection = terms.mSunDirection,
            .mSunScatter = terms.mSun.mIrradiance * 0.30f,
        };
    }
}
