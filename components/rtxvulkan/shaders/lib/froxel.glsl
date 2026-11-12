#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FROXEL_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FROXEL_GLSL

// Where the fog volume's slices stand, which four shaders have to agree about exactly.
//
// Its own file because three of the four want nothing else from the air: a shader that only asks where a
// slice starts should not pull in a phase function, a light grid and a ray query to find out.

#include "look.h"
#include "shared/medium.h"
#include "scene.h"

/// Where along the ray the slice ending at `fraction` of the way through reaches.
///
/// Squared, so the slices bunch where the fog has any shape to it: even steps over a ray that can run
/// thirty thousand units give the first hundred a twentieth of one sample, the reasoning that makes every
/// froxel grid slice its frustum exponentially rather than evenly.
float fogDepth(float fraction)
{
    return fraction * fraction;
}

/// The other way: how far through the grid a distance stands, from nought to one, clamped at the
/// reach. What a reader hands the sampler as the volume's depth coordinate.
float fogDepthInverse(float distance)
{
    return sqrt(min(distance, FOG_REACH) / FOG_REACH);
}

/// Where a pixel stands across the volume, from nought to one over the image's columns.
///
/// Normalised by the image and never by the frame: a traced view is drawn into a volume grown to the
/// largest one asked for, so the scatter pass fills the column past the frame's for the edge pixel to
/// interpolate against (`mFogTraced`).
///
/// @param pixel a position on the frame in pixels, with its half already added where a texel's
///        centre is meant.
/// @param columns how many columns and rows the volume holds, which is `mFogColumns`.
vec2 fogVolumeAcross(vec2 pixel, uvec2 columns)
{
    return pixel / float(FOG_VOLUME_SCALE) / vec2(columns);
}

/// How far in front of the eye `slice` begins and ends, in world units.
///
/// **The whole reach and not the distance to a surface**, which is the one thing a volume cannot know: it
/// is filled before anything has been traced.
float froxelNear(uint slice)
{
    return fogDepth(float(slice) / float(FOG_VOLUME_SLICES)) * FOG_REACH;
}

float froxelFar(uint slice)
{
    return fogDepth(float(slice + 1u) / float(FOG_VOLUME_SLICES)) * FOG_REACH;
}

/// How thick the grid is where a point `along` the ray stands, in world units: the stride of the slice that
/// holds it, as a smooth function of where the point is rather than of which slice that is.
///
/// Continuous, because a step in it is a shell around the eye: read at each slice's own stride, the
/// field's level steps at every slice boundary and blurs a bank's edge by one amount on the near side and
/// another beyond. The shell is the eye's, so it sweeps the ground as the eye moves, as rings around the
/// camera.
float froxelStrideAt(float along)
{
    return 2.0 * sqrt(along * FOG_REACH) / float(FOG_VOLUME_SLICES);
}

/// Where the slice stands, as the one point that answers for the whole of it.
///
/// Half way *through* the slice and not half way *between* its edges. The curve is quadratic, so those
/// are two different distances, and the first is the one the grid's depth coordinate carries, since that
/// coordinate is this curve inverted. A sampler puts texel `k` at `(k + 0.5)` of its axis; the mean of the
/// edges would have a slice near the eye ask for a fifth of a texel past its own centre.
float froxelMiddle(uint slice)
{
    return fogDepth((float(slice) + 0.5) / float(FOG_VOLUME_SLICES)) * FOG_REACH;
}

/// What the scatter pass measures at one froxel, as `fogscatter.rgen` packs it into two images and
/// `fogintegrate.comp` and `puffLight` read it back.
///
/// One record and one packing, because spelling the channels at each site let a field added or moved on
/// one side compile on the other and read a neighbour's number.
struct FogSeeing
{
    /// The sun's transport with the irradiance and the phase left off, and what a ray found of
    /// the lamp the froxel held and of the ambient over it — nought or one at an edge the grid
    /// cannot resolve, averaged over frames and neighbours. The half a puff of smoke reads.
    float mTransport;
    float mLampsSeen;
    float mAmbientSeen;

    /// The sun's transport times the density, which the air integrates (`FogSlice::mSunSource`).
    /// **Beside the transport and not in its place**: a puff in a room with no fog is lit by the sun
    /// all the same, and a product with a density of nought has no transport to give back.
    float mSunSource;
};

struct FogPoint
{
    /// What the air scatters at the point times its density, and the density (`FogSlice`).
    vec3 mSource;
    float mDensity;

    FogSeeing mSeeing;
};

vec4 packFogScatter(FogPoint point)
{
    return vec4(point.mSource, point.mDensity);
}

vec4 packFogSeeing(FogSeeing seeing)
{
    return vec4(seeing.mTransport, seeing.mLampsSeen, seeing.mAmbientSeen, seeing.mSunSource);
}

FogSeeing unpackFogSeeing(vec4 sunward)
{
    return FogSeeing(sunward.x, sunward.y, sunward.z, sunward.w);
}

FogPoint unpackFogPoint(vec4 scatter, vec4 sunward)
{
    return FogPoint(scatter.xyz, scatter.w, unpackFogSeeing(sunward));
}

vec4 packFogColumn(FogColumn column)
{
    return vec4(column.mScattered, column.mTransmittance);
}

float packFogColumnSunward(FogColumn column)
{
    return column.mSunward;
}

FogColumn unpackFogColumn(vec4 air, float sunward)
{
    return FogColumn(air.xyz, air.w, sunward);
}

vec4 packFogSlice(FogSlice slice)
{
    return vec4(slice.mSource, slice.mDensity);
}

float packFogSliceSunward(FogSlice slice)
{
    return slice.mSunSource;
}

FogSlice unpackFogSlice(vec4 slice, float sunward)
{
    return FogSlice(slice.xyz, slice.w, sunward);
}

#endif
