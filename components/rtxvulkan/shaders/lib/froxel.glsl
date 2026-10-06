#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FROXEL_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FROXEL_GLSL

// Where the fog volume's slices stand, which four shaders have to agree about exactly.
//
// **Its own file because three of the four want nothing else from the air.** `fogscatter.rgen`
// fills a froxel, `fogintegrate.comp` carries the transmittance down a column and `fogVolumeAlong`
// resolves a distance to a slice — and a shader that only asks where a slice starts should not have
// to pull in a phase function, a light grid and a ray query to find out.
//
// One statement of the curve, so a boundary the scatter pass sampled inside is the boundary the
// integrate pass takes its transmittance over.

#include "look.h"
#include "shared/medium.h"
#include "scene.h"

/// Where along the ray the slice ending at `fraction` of the way through reaches.
///
/// **Squared, so the slices bunch where the fog has any shape to it.** Even steps over a ray that
/// can run thirty thousand units give the first hundred a twentieth of one sample and lay the rest
/// across ground too far off to resolve — the same reasoning that makes every froxel grid slice its
/// frustum exponentially rather than evenly.
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
/// **Normalised by the image and never by the frame.** A traced view is drawn into a volume grown
/// to the largest one asked for, so the two are not the same number — and the scatter pass fills
/// every column the image has for exactly that reason: the pixel at the edge interpolates against
/// the column outside it.
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
/// **The whole reach and not the distance to a surface**, which is the one thing a volume cannot
/// know: it is filled before anything has been traced.
float froxelNear(uint slice)
{
    return fogDepth(float(slice) / float(FOG_VOLUME_SLICES)) * FOG_REACH;
}

float froxelFar(uint slice)
{
    return fogDepth(float(slice + 1u) / float(FOG_VOLUME_SLICES)) * FOG_REACH;
}

/// How thick the grid is where a point `along` the ray stands, in world units: the stride of the
/// slice that holds it, as a smooth function of where the point is rather than of which slice that
/// is.
///
/// **Continuous, because a step in it is a shell around the eye.** The field is read at the level
/// its sampler can resolve over this distance, and read at each slice's own stride that level steps
/// at every boundary between two slices — a bank's edge blurred by one amount on the near side of
/// the shell and by another beyond it. The shell is the eye's, so it sweeps the ground as the eye
/// moves: rings around the camera, faint while it stood still and plain as soon as it walked. The
/// derivative of the depth curve, so a slice's own middle reads exactly its own stride and the
/// sample drawn either side of it reads a little less or a little more.
float froxelStrideAt(float along)
{
    return 2.0 * sqrt(along * FOG_REACH) / float(FOG_VOLUME_SLICES);
}

/// Where the slice stands, as the one point that answers for the whole of it.
///
/// **Half way *through* the slice and not half way *between* its edges.** The curve is quadratic, so
/// those are two different distances — and the first is the one the grid's own depth coordinate
/// carries, because that coordinate is this curve inverted. What reads a volume at a distance is a
/// sampler, and a sampler puts texel `k` at `(k + 0.5)` of its axis; hand it the mean of the edges
/// instead and a slice near the eye asks for a fifth of a texel past its own centre, so a
/// reprojection that should have found the froxel it left finds a fifth of the froxel behind it.
float froxelMiddle(uint slice)
{
    return fogDepth((float(slice) + 0.5) / float(FOG_VOLUME_SLICES)) * FOG_REACH;
}

/// What one slice of a column scatters and takes out, once everything that lights it is applied, each
/// times the density (`fogDensityAt`): the air's own colour with the moons and the lamps in it, the
/// sun's transport with the irradiance and the phase left off, and the density itself, which the
/// weather's extinction makes per world unit.
///
/// **The products and not their factors** (D7): what a stretch integrates is the extinction times
/// the light, and the two move against each other — a froxel the bank's edge crosses is dense and
/// shadowed on one side and thin and lit on the other — so the mean of their product is not the
/// product of their means, which is what a history and a tent of the two apart came to.
///
/// **A sample at the slice's middle, and not a constant over the slice.** The volume holds one of
/// these per froxel, and what a froxel's value is is a property of one point in it, averaged over
/// draws — so between two of them the air is what a sampler says it is between two texels: the
/// line from one to the next. `fogThrough` integrates that line, and `fogintegrate.comp` and
/// `fogVolumeAlong` both step through it, so a column's accumulation and a pixel's read of the
/// slice it ends in agree exactly at the slice's far edge.
///
/// **Constant over a slice instead, the grid drew itself.** The accumulation was then a straight
/// line across every slice with a corner at every edge — or, reconstructed with a cubic to hide the
/// corners, a bump in the middle of every slice — and either is a pattern with the slices' own
/// period, laid on the ground as shells around the eye wherever two neighbouring slices held
/// different air. Banked air holds different air in neighbouring slices everywhere.
struct FogSlice
{
    vec3 mSource;
    float mDensity;
    float mSunSource;
};

/// What the scatter pass measures at one froxel, as `fogscatter.rgen` packs it into two images
/// and `fogintegrate.comp` and `puffLight` read it back.
///
/// **One record and one packing, because four sites spelled the channels for themselves.** The
/// writer put the transport, the lamps' seeing and the ambient's in `xyz` of one image and the
/// two readers swizzled the same letters back out, with nothing naming which was which. A field
/// added or moved on one side compiled on the other and read a neighbour's number.
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

/// The accumulation up to a slice's far edge, as `fogintegrate.comp` packs it and
/// `fogVolumeAlong` reads it: what scattered in, what is left of the ray, and the sun's transport
/// alone.
struct FogColumn
{
    vec3 mScattered;
    float mTransmittance;
    float mSunward;
};

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

FogSlice fogSliceBetween(FogSlice from, FogSlice to, float fraction)
{
    return FogSlice(mix(from.mSource, to.mSource, fraction), mix(from.mDensity, to.mDensity, fraction),
        mix(from.mSunSource, to.mSunSource, fraction));
}

/// Carries a ray `length` units through air of `slice`, under the weather's `extinction` per unit of
/// density, accumulating what it scattered in and taking off what it lost.
///
/// **The source is the extinction times the light, so what a stretch scatters in is
/// `T (1 - e^-σd) / σ` of it** — the transmittance's share the source keeps over the stretch —
/// which `mediumKept` holds to its digits however thin the air, and which is the stretch's length times
/// the transmittance where there is no air at all. What the transmittance loses is the same stretch
/// times `σ`.
///
/// **The transmittance exact over a stretch the line does not bend in, and the light scattered in
/// the midpoint rule's, second order in the stretch.** A linear density's mean is its middle, so the
/// optical depth is exact; the light a linear source scatters through a linear density has a closed
/// form only through `erf`, and the middle's source over the stretch's loss is its midpoint estimate.
/// Which is why both callers cut a slice at its middle: the line from one slice's sample to the next
/// bends only at the samples, so each half of a slice is one straight piece.
void fogThrough(
    inout float transmittance, inout vec3 scattered, inout float sunward, FogSlice slice, float length, float extinction)
{
    const float depth = slice.mDensity * extinction * length;
    const float share = transmittance * mediumKept(depth);

    scattered += share * length * extinction * slice.mSource;
    sunward += share * length * extinction * slice.mSunSource;
    transmittance -= share * depth;
}

#endif
