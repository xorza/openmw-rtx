#include "skylight.hpp"

#include <algorithm>
#include <cmath>

#include <osg/Math>

#include <components/rtx/image/colour.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/sky/sundisc.hpp>

namespace Rtx
{
    namespace
    {
        /// How long the day is, in hours.
        ///
        /// **A night that begins before the sunrise it followed belongs to the next day**, which is
        /// the wrap every hour of this file is read through: an hour before dawn is late in the
        /// previous night rather than early in a day it has not reached.
        float dayLength(const Sky::TimeOfDaySettings& times)
        {
            const float nightStart
                = times.mNightStart < times.mNightEnd ? times.mNightStart + 24.0f : times.mNightStart;

            return nightStart - times.mNightEnd;
        }

        /// How much of the hour's own darkness the exposure keeps, as a power of the light it gives: two
        /// stops and four fifths between a clear noon and a clear midnight, which are 8.90 stops apart by
        /// luminance. Under the literature's four and a half, because noon to midnight is five hundred to
        /// one here against the world's hundred million; a Purkinje shift buys the rest.
        constexpr float sHourStops = 0.314f;

        /// How much of a room's recorded ambient a traced bounce's landing stands in for the bounces
        /// past it with. Set by eye: at one, a room with few lamps is lit by this alone and shows no
        /// shape, and at nought a corridor measured with no lamp in reach stood 60% black.
        constexpr float sRoomFillBounced = 0.18f;

        /// How much of a room's recorded sunlight a traced bounce's landing takes beside its ambient.
        /// There for the rooms whose record says their light in its sunlight, as `Akulakhan's Chamber`
        /// does, with an ambient a third of a usual room's: the landing's ambient alone left it 66% black.
        /// Small, since whole it was the flat light of a room with few lamps; at this share a room whose
        /// sunlight is small hardly moves (`docs/rtx/research/2026-11-05-room-ambient.md`).
        constexpr float sRoomSpreadBounced = 0.14f;

        /// How much of a room's recorded ambient and sunlight stands on the surface itself where no
        /// bounce is traced. Reached with no landing's albedo in front of it, so far less than
        /// `sRoomFillBounced`, and moved with it, so the two pictures stay matched. Fit to one bounce's
        /// picture over eight rooms under vanilla, a mod list and PBR replacers, at a fixed exposure, with
        /// each within a third of a stop but `Akulakhan's Chamber`, which the glow its bounces find lights.
        constexpr float sRoomAmbientUnbounced = 0.035f;

        /// How much of a room's recorded ambient and sunlight lights the water in front of the eye. Seen
        /// directly, with no albedo in front of it. Set by eye: at the landing's share a flooded room with
        /// no lamp stood almost black, and at the whole record the water outshone walls that lost most of
        /// their ambient (`docs/rtx/research/2026-11-05-room-ambient.md`).
        constexpr float sRoomWaterAmbient = 0.5f;

        /// A sun out of a weather's reading and however much of the disc the asker can see — the
        /// ground's share and a cloud deck's are the same sun at the same place and colour.
        Sun sunAbove(const SkyReading& sky, float share)
        {
            return Sun{
                .mPosition = sky.mSunPosition,
                .mIrradiance = sky.mSunColour * (Shaders::DAYLIGHT * std::clamp(share, 0.0f, 1.0f)),

                // The glare is a blend factor the rasterizer applies to a sprite in the file's own space,
                // and dimming radiance is a linear multiply: applied before the decode it would come out
                // a different colour, not merely a darker one.
                .mDiscColour = sky.mDiscColour * sky.mGlare,
            };
        }

        /// Rayleigh optical depth at the zenith, at the three sRGB primaries: `0.008569 λ^-4` with
        /// its usual correction, at 600, 550 and 450 nanometres. Aerosol is left out, because how
        /// thick the haze is belongs to a weather.
        const osg::Vec3f sAirDepth(0.0683f, 0.0973f, 0.2213f);

        /// How far the world curves under that layer — the Earth's own radius, in world units. Not
        /// `CloudShell::mCurvature`, which is a shape fit off Morrowind's cap and read as `h / R`
        /// is a world 128 times too small.
        const float sWorldRadius = 6371000.0f * Constants::UnitsPerMeter;
    }

    float exposureBias(const osg::Vec3f& sunIrradiance, const osg::Vec3f& ambient)
    {
        const float level = (sunIrradiance + ambient) * Shaders::LUMINANCE_WEIGHTS;

        // Against a full sun rather than a noon worked out here, so no second number must be kept in step
        // with `DAYLIGHT`: a clear noon delivers 8.03 against its 8.
        return std::pow(std::min(level / Shaders::DAYLIGHT, 1.0f), sHourStops);
    }

    osg::Vec3f airTransmittance(float upward)
    {
        // Already the sine of the elevation; the fit below wants that and the angle, and only the angle
        // costs a trig call.
        const float sine = std::clamp(upward, 0.0f, 1.0f);
        const float elevation = osg::RadiansToDegrees(std::asin(sine));

        // Kasten and Young's fit, which holds to the horizon where `1 / sin` runs away: 37.92 air
        // masses there against one overhead.
        const float mass = 1.0f / (sine + 0.50572f * std::pow(elevation + 6.07995f, -1.6364f));

        return osg::Vec3f(
            std::exp(-sAirDepth.x() * mass), std::exp(-sAirDepth.y() * mass), std::exp(-sAirDepth.z() * mass));
    }

    SkyBudget skyBudget(const osg::Vec3f& horizon, const osg::Vec3f& zenith, const float zenithShare,
        const osg::Vec3f& sheets, const osg::Vec3f& ambient)
    {
        // What a uniform sky would have to be to deliver what this gradient does: its
        // cosine-weighted integral over the hemisphere is `pi` times the two colours weighed by
        // `Atmosphere::mZenithShare`. The sheets are already a mean over the hemisphere.
        const osg::Vec3f carried = horizon * (1.0f - zenithShare) + zenith * zenithShare + sheets;

        const osg::Vec3f fill(std::max(ambient.x() - carried.x(), 0.0f), std::max(ambient.y() - carried.y(), 0.0f),
            std::max(ambient.z() - carried.z(), 0.0f));

        return SkyBudget{ .mMean = carried + fill, .mFill = fill };
    }

    Skylight makeSkylight(const SkyReading& sky)
    {
        const osg::Vec3f irradiance = sky.mSunColour * Shaders::DAYLIGHT;
        const float share = std::clamp(sky.mSunShare, 0.0f, 1.0f);

        // A dusk's share: nothing at noon, where the direct term carries all, and nothing at night, where
        // there is no sun; a half at the half-set point. Not `1 - share`, which is largest where there is
        // no sun: `Sun_Night_Color` is the engine's stand-in for moonlight, spread as an ambient it came to
        // six times the recorded night ambient, and this renderer traces the moons.
        const float dusk = 2.0f * share * (1.0f - share);

        Skylight light{
            .mSun = sunAbove(sky, share),
            .mSunAloft = sunAbove(sky, sky.mSunShareAloft),
            .mAmbient = sky.mAmbient + irradiance * (dusk * Shaders::INV_FOUR_PI),
        };
        light.mSurfaceAmbient = light.mAmbient;
        light.mWaterAmbient = light.mAmbient;

        // **The day's gain is adapted to in full.** A lifted frame meters `gain` brighter and the exposure
        // answers with `gain^-EXPOSURE_ADAPTATION`, so the rest of the gain is taken off here. The hour's
        // own bias is measured on the unlifted terms, because it measures what they come to between them.
        light.mDaylightGain = std::pow(Shaders::DAYLIGHT_GAIN, share);
        light.mExposureBias = exposureBias(light.mSun.mIrradiance, light.mAmbient)
            * std::pow(light.mDaylightGain, Shaders::EXPOSURE_ADAPTATION - 1.0f)
            * std::exp2(Shaders::EXPOSURE_LIT_STOPS * share);

        return light;
    }

    float sunShareAt(float hour, const Sky::TimeOfDaySettings& times)
    {
        // **The weather manager's own two rules, read from where it draws by them.** Its ramp runs on
        // through the night, since a rasterizer that has hidden the disc has no use for it; a tracer asks
        // this to decide whether to cast a shadow, so the gate that hides the disc is folded in, and the
        // ramp is bounded because it passes one where a sunrise window is longer than an hour.
        if (!Sky::sunUp(hour, times))
            return 0.0f;

        return std::min(1.0f, Sky::sunDiscAlpha(hour, times));
    }

    float sunDescentPerHour(const Sky::TimeOfDaySettings& times)
    {
        const float day = dayLength(times);
        if (!(day > 0.0f))
            return 0.0f;

        // The disc stands at `swing - |east|` over a horizontal `hypot(swing, northing)`, so near
        // either end its elevation is that ratio times what the orbit has left to run — and the
        // orbit crosses two units over the whole day (`Sky::sunDirection`).
        return 2.0f * Sky::sSunSwing / std::hypot(Sky::sSunSwing, Sky::sSunNorthing) / day;
    }

    float sunShareAloft(float hour, const Sky::TimeOfDaySettings& times)
    {
        // How far under the ground's horizon a layer that high still sees the sun, and how long the
        // disc takes to fall that far.
        const float dip = std::sqrt(2.0f * sCloudAltitude / sWorldRadius);
        const float descent = sunDescentPerHour(times);
        if (!(descent > 0.0f))
            return sunShareAt(hour, times);

        // The larger of the ramp read either side, because the layer's day is the ground's widened at both
        // ends: earlier in the morning and later in the evening are opposite shifts of one clock.
        const float offset = dip / descent;

        return std::max(sunShareAt(hour - offset, times), sunShareAt(hour + offset, times));
    }

    Daylight makeRoomLight(const ESM::Cell::AMBIstruct& room)
    {
        const osg::Vec3f haze = decodeColour(room.mFog);
        const osg::Vec3f fill = decodeColour(room.mAmbient);

        // The record's sunlight, put where light with no direction belongs, by the factor
        // `makeSkylight` spreads a dusk's sun with: a room has no sky to take a direction from.
        const osg::Vec3f spread = decodeColour(room.mSunlight) * (Shaders::DAYLIGHT * Shaders::INV_FOUR_PI);

        return Daylight{
            .mLight = Skylight{ .mAmbient = fill * sRoomFillBounced + spread * sRoomSpreadBounced,
                .mSurfaceAmbient = (fill + spread) * sRoomAmbientUnbounced,
                .mWaterAmbient = (fill + spread) * sRoomWaterAmbient,
                .mExposureBias = std::exp2(Shaders::EXPOSURE_LIT_STOPS) },
            .mSkyHorizon = haze,
            .mSkyZenith = haze,
            .mStarFade = 0.0f,
            .mFog = roomFog(haze, room.mFogDensity),
        };
    }
}
