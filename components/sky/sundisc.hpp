#pragma once

#include <osg/Vec3f>

namespace Sky
{
    struct TimeOfDaySettings;

    /// Morrowind's own sun, hardcoded in the engine it came from: how far east and west its
    /// direction swings, how far north it sits, and how far down it points. Named once for the
    /// weather manager that places it, the disc drawn along it, and the ray tracer's day.
    inline constexpr float sSunSwing = 400.f;
    inline constexpr float sSunNorthing = 75.f;
    inline constexpr float sSunDepth = -100.f;

    /// The sun's direction at `orbit`, from -1 at one end of its day or night to 1 at the other:
    /// `MWWorld::WeatherManager::update`'s own.
    inline osg::Vec3f sunDirection(float orbit)
    {
        return osg::Vec3f(-sSunSwing * orbit, sSunNorthing, sSunDepth);
    }

    /// Whether the sun is drawn at `hour`: `MWWorld::WeatherManager::update`'s own gate, which hides
    /// the disc from the night's start to its end. Lifted here so the renderer that asks whether
    /// there is a sun to cast a shadow from reads the rule the weather manager draws by.
    bool sunUp(float hour, const TimeOfDaySettings& times);

    /// How much of the disc is there at `hour`, as `MWWorld::WeatherManager::calculateResult` writes
    /// it into the disc colour's alpha: squared out across dusk, linear in over the first half of
    /// the sunrise window — the hour past dawn and not a fraction of it, so a window longer than an
    /// hour passes one — and one through the day. Runs on through the night and comes back at one,
    /// because a rasterizer that has hidden the disc has no use for the answer; `sunUp` is the
    /// other half.
    float sunDiscAlpha(float hour, const TimeOfDaySettings& times);

    /// Where the disc is drawn for the orbit's `direction`: the direction reversed, and its height
    /// bent down as it leaves the zenith by Morrowind's own rule, which `RenderingManager` applied
    /// before handing the dome its sun. One statement, because the light and the disc are placed
    /// by two renderers off one direction, and the disc has to be where the shadows say it is.
    osg::Vec3f sunDiscPosition(const osg::Vec3f& direction);
}
