#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PUFF_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PUFF_GLSL

// How a puff of smoke is lit and what a layer of puffs holds: the sprites' march (`sprites.glsl`) and a
// medium's shells (`medium.glsl`) each light and lay theirs by these, and only the first marches sprites.

#include "colour.h"
#include "look.h"
#include "scene.h"
#include "bindings.glsl"
#include "frame.glsl"
#include "froxel.glsl"
#include "underwater.glsl"
#include "variants.glsl"

/// What a puff's own shape and its own texture leave of each of the two terms `puffLight` reads.
///
/// **Two and not one per light, because only the sun keeps a direction.** The volume stores its transport
/// without the irradiance or the phase, so a puff can take a side toward the sun; the ambient and the lamps
/// arrive with no direction a shape could take a side toward, and one mean meets all of it.
struct PuffShape
{
    /// What the puff leaves of the sun: its own side toward it, its texture's own thickness across
    /// it, and the layers of its own emitter between it and the sun.
    float mSunLit;

    /// The same for everything else the air scatters. A ball takes a side toward the sky here and
    /// nothing toward the lamps, which is what an ambient with no direction in it can be met with.
    float mAmbientLit;
};

/// What a cylinder takes of a source at right angles to it, against its own mean over every
/// direction. The mean of `sin` over a sphere is `pi / 4`, so this is what makes a shape of one the
/// even share here as it is for a ball.
const float CYLINDER_SHARE = 4.0 * INV_PI;

/// The shape of a streak of rain, which is a cylinder's own share of what lights it.
///
/// **The march draws the streak as a cylinder, so it is lit as one.** Its width is swung about its axis to
/// meet the ray, a cylinder's silhouette, and a cylinder presents `sin` of the angle off its axis to a source,
/// broadside: rain under a low sun takes a quarter more than the even share, under a high one two thirds of
/// it, and a streak looked at end-on takes none.
///
/// **The rasterizer's answer is not this, and is not copied.** `osgParticle` commits a `FIXED` quad to the
/// plane its two authored axes span, so every raindrop wears one normal (world north) and the storm is lit by
/// how far the sun stands from it; this walk refuses that already when it swings the width.
///
/// **The ambient keeps the even share**: a standing streak sees the sky from every side, and only a source
/// with a direction can be met broadside (`ballPuff` treats the sky as one because a ball has a side to turn
/// toward it).
///
/// @param along unit, the axis the streak hangs on.
PuffShape streakPuff(vec3 along)
{
    return PuffShape(CYLINDER_SHARE * length(cross(along, frame.mSun.mDirection)), 1.0);
}

/// What a puff of smoke is lit by, per unit of albedo, out of the froxel it stands in.
///
/// **A puff is the same kind of thing the air is, in the same place, so it is lit by the same answers.**
/// `Rtx::FogVolume` holds, per point and averaged over frames, what a shadow ray found toward the sun, what
/// the one lamp worth a ray delivered and whether it was seen, and what the ambient's ray found over the whole
/// sphere: two fetches where three rays of the puff's own would be.
///
/// **What a field buys is not the cost.** Rays cast at one puff of a layer and shared with the rest flip
/// between neighbouring pixels wherever the layer straddles a shadow edge, and draw that puff's silhouette (a
/// black disc through a drain's splash at Vivec). A field the sampler interpolates cannot, and one averaged
/// over frames alone could still speckle: M[FR]'s ground mist showed each froxel's draw as a warm blotch a
/// column wide, so this reads `fogSeeing` and not what the scatter pass wrote.
///
/// **The three terms are `pathEnd`'s arithmetic with the visibilities read rather than traced**: the ambient
/// by what the point sees of it, the sun by its transport (the shadow and the beam through the fog), and the
/// lamps by what they deliver times whether they are seen.
///
/// **As bright as a card of the same albedo, and that is not a fudge.** An opaque diffuse *sphere* would catch
/// `pi r^2` of the beam and radiate over `4 pi r^2`, a quarter of the facing value, but a puff is a cloud of
/// droplets that scatters strongly forward and again inside itself, so the sun reaches all of it. The quarter
/// put a plume back at the sky's own ambient, where it was invisible.
///
/// @param seen how far along the ray the puff stands, which with the pixel names the froxel.
/// @param wrapped what the puff's own shape and its own texture leave of each of the two terms —
///        `PuffShape`, whose fields say which is which.
vec3 puffLight(uvec2 pixel, vec3 direction, float seen, PuffShape wrapped)
{
    // The column this pixel stands in and the depth the puff stands at. The level named for the
    // reason `fogSliceAt` gives.
    const vec3 at = vec3(fogVolumeAcross(vec2(pixel) + 0.5, frame.mFogColumns), fogDepthInverse(seen));

    const FogSeeing seeing = unpackFogSeeing(textureLod(fogSeeing, at, 0.0));

    // **The field holds the air's share and a puff takes the card's.** `lampsInAir` stores the lamps' mean
    // irradiance under the air's isotropic phase, `E / 4 pi`; a card is lit at `E / pi`, as the sun and the
    // fill are below, so a puff takes four of what the air does. Stored in the air's own quantity because the
    // field is half floats and a lamp's irradiance beside it runs twelve times closer to their top.
    const vec3 lamps = textureLod(fogLamps, at, 0.0).xyz * (INV_PI / INV_FOUR_PI);

    const vec3 daylight = daylightReaching(frame.mOrigin + direction * seen);

    const vec3 sun
        = HAS_SUN ? frame.mSun.mIrradiance * daylight * (seeing.mTransport * INV_PI * wrapped.mSunLit) : vec3(0.0);

    return frame.mAmbient * daylight * (seeing.mAmbientSeen * wrapped.mAmbientLit) + sun
        + lamps * (seeing.mLampsSeen * wrapped.mAmbientLit);
}

/// What a painted alpha hides over `crossings` of the thickness it was painted for.
///
/// **A painted alpha is an optical depth and not a coverage**: a ray through part of one crossing hides less
/// and through more than one hides more, both `1 - (1 - a) ^ n`. A sprite's `n` is the share of its own chord
/// the eye sees: one in the open, a sliver where the ball runs into a wall.
///
/// **No test for a whole crossing here**: a ball's share of its own chord is one only up to rounding, so a
/// test on it was a float equality a warp's balls took both ways; a streak is whole by construction, and
/// `paintedWhole` is what it takes, on a test that is the emitter's and so uniform across its run.
float paintedOver(float painted, float crossings)
{
    return 1.0 - pow(1.0 - min(painted, SPRITE_ALPHA_LIMIT), crossings);
}

/// The same per channel, for a flame that absorbs as much as it emits in each of them.
vec3 paintedOver(vec3 painted, float crossings)
{
    return 1.0 - pow(1.0 - min(painted, vec3(SPRITE_ALPHA_LIMIT)), vec3(crossings));
}

/// What a painted alpha hides over exactly one crossing, which needs no power: the limit alone.
float paintedWhole(float painted)
{
    return min(painted, SPRITE_ALPHA_LIMIT);
}

vec3 paintedWhole(vec3 painted)
{
    return min(painted, vec3(SPRITE_ALPHA_LIMIT));
}

/// How a ball is lit from `toward` against its mean, on the side of it the eye sees.
float ballWrap(vec3 normal, vec3 toward)
{
    return 1.0 + SPRITE_WRAP * dot(normal, toward);
}

/// How much more of the sun a puff of smoke throws toward the eye than an even share would.
float smokeThrow(vec3 direction)
{
    return henyeyGreenstein(SMOKE_ANISOTROPY, dot(frame.mSun.mDirection, direction)) / INV_FOUR_PI;
}

/// The shape of a ball of smoke met at `normal`: the sun thrown forward and wrapped round the side the sun
/// is on, and the sky's side.
///
/// **The sky's side, and only as much of it as the frame's ambient is the sky's.** Nothing a puff is lit by
/// besides the sun has a direction to take a side toward, except that out of doors the ambient is nearly the
/// sky, which is above; a room's fill arrives from every side and a ball meets it evenly, which is what the
/// mix says at nought.
///
/// **Two callers, which keeps them one shape**: a sprite's ball, and a cloud modelled as shells
/// (`mediumAlong`), which is smoke with a real plane to read the side off.
///
/// @param thrownForward `smokeThrow` for the ray, which a walk evaluates once for every ball on it.
PuffShape ballPuff(vec3 normal, float thrownForward)
{
    return PuffShape(thrownForward * ballWrap(normal, frame.mSun.mDirection),
        mix(1.0, ballWrap(normal, vec3(0.0, 0.0, 1.0)), frame.mAmbientFromSky));
}

/// What a sprite's own texture lets through to a point on it from `toward`, out of its six-way bake.
///
/// **Six directions, weighted by how much of `toward` lies along each and divided by the same weights**, so
/// that a texel nothing shadows is lit in full from anywhere.
///
/// @param toward unit, from the sprite toward the light.
/// @param planeAcross,planeUp the sprite's own `u` and `v` in the world, which for a disc are the
///        ray's own, square to it.
/// @param facing where the eye is, unit, from the sprite.
/// @param shade the bake at this texel, already thinned by the sprite's own fade.
float sixWayThrough(vec3 toward, vec3 planeAcross, vec3 planeUp, vec3 facing, vec4 shade, float back)
{
    const vec3 along = vec3(dot(toward, planeAcross), dot(toward, planeUp), dot(toward, facing));
    const vec3 positive = max(along, vec3(0.0));
    const vec3 negative = max(-along, vec3(0.0));

    // Never nought: `toward` is unit, so at least one of the six lies along it.
    const float weight = dot(positive + negative, vec3(1.0));

    return (positive.x * shade.x + negative.x * shade.y + positive.y * shade.z + negative.y * shade.w + positive.z
               + negative.z * back)
        / weight;
}

/// What the puffs between the eye and a surface add to the frame, and what they leave of it.
///
/// **Two walks fill one of these**, because the two are the same kind of thing: `spritesAlong` gathers what
/// an emitter drew, and `mediumAlong` gathers the shells of a cloud the content modelled as geometry.
struct PuffLayer
{
    /// What the covering puffs look like where they cover a pixel whole — a straight colour and not one
    /// premultiplied by `1 - mTransmittance`.
    ///
    /// **Straight**, because the composite multiplies it by the coverage where it composites.
    vec3 mColour;

    /// What the additive sprites put in, which no coverage carries.
    ///
    /// **Apart from `mColour`, because an alpha blend cannot express a flame**: a blended layer is a colour and
    /// an opacity, and a flame's opacity is nought, so what it adds rides with the frame behind it. The cost
    /// is that a plume across a flame dims it.
    vec3 mAdded;

    /// What survives the covering puffs. One for a frame with only flames in it: additive
    /// blending hides nothing by definition.
    float mTransmittance;

    /// How far along the ray the covering happened, at each puff's share of what the layer shows: in order
    /// for the sprites' nearest four (`PuffLayers`), and by alpha for a medium's shells, which have none.
    ///
    /// **What the caller splits the air at.** A layer that covers what is behind it must not cover the haze
    /// in front of it, and the depth the colour came from is the one to split at. Nought for a frame that
    /// covered nothing.
    float mCoveredAt;

    /// The sum of the covering puffs' alphas: what `mergedPuffs` weighs two layers by, and what a
    /// medium's walk weighed `mColour` and `mCoveredAt` by.
    float mWeight;
};

/// A layer with nothing in it, which is what both walks start from and what either answers with
/// where it found nothing.
PuffLayer noPuffs()
{
    PuffLayer layer;
    layer.mColour = vec3(0.0);
    layer.mAdded = vec3(0.0);
    layer.mTransmittance = 1.0;
    layer.mCoveredAt = 0.0;
    layer.mWeight = 0.0;

    return layer;
}

#endif
