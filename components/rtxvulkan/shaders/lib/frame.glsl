#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FRAME_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FRAME_GLSL

// What this frame says about the world, asked one way.
//
// Each of these is one spelling of a test that would otherwise be written out in several places (whether
// there is a sun, whether a point stands under the water), where a frame that changed how it said one
// would have to find every copy.

#include "bindings.glsl"
#include "variants.glsl"

/// Whether the sun is over the horizon: lighting, casting, and drawn as a disc.
bool sunUp()
{
    return HAS_SUN && frame.mSun.mIrradiance != vec3(0.0);
}

/// Whether the sky is a light: the ambient is the sky's and a ray that leaves the world finds it.
///
/// **What `mAmbientFromSky` decides, and it decides three things**: whether a bounce that escapes brings
/// anything back, how far the ambient looks for what occludes it, and whether an indirect hit is rated.
bool skyLights()
{
    return frame.mAmbientFromSky > 0.0;
}

/// How much water stands over a point, in world units — or nothing at all above the surface, and in a cell
/// that has none.
///
/// One subtraction, so a dry cell's sentinel is read in one place: the level is minus infinity where there
/// is no water, and `HAS_SEA` takes the whole test out of a kernel that was built for a room.
float waterOver(vec3 position)
{
    return HAS_SEA ? max(frame.mWaterLevel - position.z, 0.0) : 0.0;
}

/// How far a ray that starts in the water stays in it, over the `distance` it runs: all of it where it runs
/// level or down, and as far as it rises through the plane where it climbs.
///
/// The plane and not the surface's mesh, because the plane is what `waterOver` measures and has absolute
/// sides: a ray from under the water that rises where no mesh stands crosses no surface, and is in air
/// from the plane on all the same.
float waterAlong(vec3 from, vec3 direction, float distance)
{
    const float rise = waterOver(from) / max(direction.z, 1.0e-20);
    return direction.z > 0.0 ? min(distance, rise) : distance;
}

/// Whether a ray that found nothing was under the surface looking down, which is water and not sky.
///
/// The plane has absolute sides, so below it there is water whether or not this renderer was handed a bed
/// far enough out to stop the ray. Asked where the sky is drawn and again where the column is measured
/// (`missedAlong`, `tracePixel`, the media stage): a plane test and a sign, which an answer field carrying
/// it between them would cost more than.
bool waterUnbounded(bool found, vec3 origin, vec3 direction)
{
    return !found && direction.z < 0.0 && waterOver(origin) > 0.0;
}

/// The eye a ray the kernel cast went through: `frame.mEyes.mArms` where `arms` is one, the world's
/// `frame.mEyes.mWorld` where it is nought.
Camera eyeOf(uint arms)
{
    return arms != 0u ? frame.mEyes.mArms : frame.mEyes.mWorld;
}

/// How much wider that eye's image plane is than the world eye's, per axis, which is what a point
/// seen through it reprojects through: `frame.mArmsSpread` for the arms, and one for the world.
vec2 eyeSpread(uint arms)
{
    return arms != 0u ? frame.mArmsSpread : vec2(1.0);
}

#endif
