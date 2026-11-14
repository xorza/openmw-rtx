#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SPRITES_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SPRITES_GLSL

// The particle layer, marched against the primary ray rather than built into an
// acceleration structure.

#include "colour.h"
#include "look.h"
#include "scene.h"
#include "bindings.glsl"
#include "fog.glsl"
#include "frame.glsl"
#include "puff.glsl"
#include "underwater.glsl"
#include "variants.glsl"

/// How much of the sprite's rim the mip chain has already eaten, as a factor to taper it by.
///
/// **A sprite a few pixels across is sampled several levels down its own chain**, where the painted blob is
/// averaged into a flat wash and only the square the texture was cut to is left, so a spark at any distance
/// reads as a little rectangle.
float spriteTaper(float radial, float lod)
{
    // Written the way round GLSL defines: `smoothstep` with its first edge above its second is
    // undefined, however reliably it happens to produce the descending ramp.
    return mix(1.0, 1.0 - smoothstep(SPRITE_TAPER_START, 1.0, radial), clamp(0.5 * lod, 0.0, 1.0));
}

/// Where a ray crosses one sprite: the things the rest of the walk needs, and the only place the
/// two kinds of sprite differ.
struct SpriteCrossing
{
    /// Whether the ray meets the sprite at all before `limit`.
    bool mFound;

    /// How far along the ray the eye sees the sprite, and what share of the sprite's own depth
    /// that is.
    float mSeen;
    float mFraction;

    /// Where across the sprite the ray crossed, in units of its own half-extents, and how far out
    /// that is as a fraction of the radius.
    vec2 mAt;
    float mRadial;

    /// How far the sprite's surface stands toward the eye there. Nought for a quad.
    float mLift;

    /// How densely the sprite carries texels, per unit of the cone's width where it stands.
    float mRate;

    /// The axis a streak hangs on, unit. Nought for a ball, which hangs on nothing.
    vec3 mAlong;
};

/// A crossing the ray did not make.
SpriteCrossing noCrossing()
{
    return SpriteCrossing(false, 0.0, 0.0, vec2(0.0), 0.0, 0.0, 0.0, vec3(0.0));
}

/// **A quad that hangs in the world**, so the ray meets a plane rather than a ball.
///
/// **The axis it hangs on is the sprite's; which way its width faces is not.** `osgParticle` commits a
/// `FIXED` system's quad to the plane its two axes span and Morrowind's rain commits it to the world's X–Z
/// one, so a drop looked at from along X is a polygon seen edge-on and the same storm reads three times
/// heavier facing north than facing east. That is a fact about drawing quads and not about rain.
///
/// @param width how wide the emitter's quads are against their own axis, `GpuEmitter::mWidth`.
/// @param texels the texture's extent along each of the quad's axes.
SpriteCrossing quadCrossing(GpuSprite sprite, vec3 toSprite, vec3 direction, float limit, float width, vec2 texels)
{
    const vec3 axis = sprite.mAxis;
    const vec3 swung = cross(axis, direction);
    const float swing = length(swung);

    // Looking straight down the streak's own axis, where no swing presents any width: the quad is
    // edge-on to this ray and there is nothing of it to see.
    if (swing <= 1.0e-4)
        return noCrossing();

    const float inverseAxis = inversesqrt(dot(axis, axis));

    const vec3 quadAcross = swung * (width * sprite.mRadius / swing);
    const vec3 quadUpward = axis * sprite.mRadius;
    const vec3 normal = cross(quadAcross, quadUpward);

    const float facing = dot(normal, direction);
    if (abs(facing) <= 1.0e-6)
        return noCrossing();

    const float depth = dot(toSprite, normal) / facing;
    if (depth <= 0.0 || depth >= limit)
        return noCrossing();

    const vec3 offset = direction * depth - toSprite;
    const vec2 at = vec2(
        dot(offset, quadAcross) / dot(quadAcross, quadAcross), dot(offset, quadUpward) / dot(quadUpward, quadUpward));
    if (max(abs(at.x), abs(at.y)) >= 1.0)
        return noCrossing();

    // **Both of the quad's own axes, and the denser one decides.** A rain streak carries eight texels across a
    // fifth of its own height and thirty-two down the whole of it, so its width resolves two and a half times
    // finer than its length, and a level chosen from the length alone reads the width sharper than the ray can
    // carry: a drop that aliases into a hard mark instead of fading. A disc is the same extent both ways.
    //
    // **The length as the ray sees it**: a cone across the ray covers `1 / sin θ` of an axis θ off it, which is
    // rain seen from above, and `sin θ` is `swing` over the axis's length, so the axis's own `inverseAxis`
    // cancels.
    const float rate = 0.5 * max(texels.x / width, texels.y / swing) / sprite.mRadius;

    return SpriteCrossing(true, depth, 1.0, at, length(at), 0.0, rate, axis * inverseAxis);
}

/// **A ball and not a disc, and what the eye sees of it is a chord.** The disc the rasterizer drew is the
/// ball's silhouette and everything it painted is kept: in the open the chord is whole and the sprite
/// composites exactly as the quad did. What the ball adds is an inside, so where it runs into a wall (or the
/// wall into it, or the eye into either) the chord is cut at the surface and the sprite fades along the ray
/// instead of being clipped at its centre.
///
/// **A first-person effect is the billboard and has no inside**: a ball's near half lets a particle centred
/// behind the palm shine through its front, so it stands at its centre, whole or not at all. Selected, so an
/// emitter on the arms takes the path every other takes.
///
/// @param across,upward the disc's own axes, unit and square to the ray, which the disc's texture
///        is read along: `spritesAlong` says why they are not the screen's.
/// @param billboard whether the sprite's emitter is on the arms (`EMITTER_FIRST_PERSON`).
SpriteCrossing ballCrossing(
    GpuSprite sprite, vec3 toSprite, vec3 direction, float limit, vec3 across, vec3 upward, vec2 texels, bool billboard)
{
    const float depth = dot(toSprite, direction);
    const vec3 offset = toSprite - direction * depth;
    const float radial2 = dot(offset, offset) / (sprite.mRadius * sprite.mRadius);
    if (radial2 >= 1.0)
        return noCrossing();

    const float lift = sqrt(1.0 - radial2);
    const float halfChord = sprite.mRadius * lift;
    const float from = max(depth - halfChord, 0.0);
    const float until = min(depth + halfChord, limit);
    const bool crossed = billboard ? depth > 0.0 && depth < limit : until > from;
    if (!crossed)
        return noCrossing();

    const vec2 at = -vec2(dot(offset, across), dot(offset, upward)) / sprite.mRadius;
    const float rate = 0.5 * max(texels.x, texels.y) / sprite.mRadius;

    return SpriteCrossing(true, billboard ? depth : 0.5 * (from + until),
        billboard ? 1.0 : (until - from) / (2.0 * halfChord), at, sqrt(radial2), lift, rate, vec3(0.0));
}

/// The pixel whose tile a ray looks its sprites up in: its own for the world's eye, and for the arms' the
/// world pixel the ray passes through, held to the frame.
///
/// **The bin is the world camera's.** An arms' ray leaves the same eye across a plane of the arms' own field
/// of view, so the pixel it was cast for names a tile the world's ray through that pixel meets, and not the
/// tile of what the arms' ray meets. Mapped for every ray and selected, so a warp over the hand's edge takes
/// one path.
///
/// @param direction the ray's, ahead of the eye.
uvec2 binnedPixel(uvec2 pixel, vec3 direction, bool arms)
{
    const Camera world = frame.mEyes.mWorld;
    const Screen screen = screenOf(frame.mScreen, direction, vec2(1.0));

    // The pixel whose area the ray crosses the plane in.
    const vec2 across = pixelOfScreen(screen, vec2(world.mWidth, world.mHeight)) - world.mJitter;
    const uvec2 through
        = uvec2(clamp(floor(across), vec2(0.0), vec2(float(world.mWidth - 1u), float(world.mHeight - 1u))));

    return arms ? through : pixel;
}

/// One covering puff a walk keeps apart: its light, already multiplied by its own coverage, and the
/// coverage; and how far along the ray it stands. An empty one stands past every puff, so it sorts
/// last and merging it changes nothing.
struct KeptPuff
{
    vec4 mLight;
    float mAt;
};

KeptPuff noPuff()
{
    return KeptPuff(vec4(0.0), 3.0e38);
}

/// The covering puffs a walk has met, the nearest four of them apart and in order of depth, and every one
/// past them merged into the farthest: Salvi and Vaidyanathan's multi-layer alpha blending (2014).
///
/// **Four and not two, because the two cost the same.** Measured on the skies suite, three alternated rounds
/// each: the order took 0.02 to 0.04 ms from the trace's median against the weighted mean before it (the
/// storm's 1.25 to 1.28 ms became 1.29 to 1.31), and two layers took no less than four, within a hundredth.
///
/// **In order and not weighed by alpha alone**, because a coverage-weighted mean lets a farther, denser layer
/// win: a thin wisp in front of a thick plume came out the plume's colour.
struct PuffLayers
{
    KeptPuff mNearest;
    KeptPuff mSecond;
    KeptPuff mThird;
    KeptPuff mFarthest;
    KeptPuff mIncoming;
};

PuffLayers noPuffLayers()
{
    return PuffLayers(noPuff(), noPuff(), noPuff(), noPuff(), noPuff());
}

/// `front` and `back` in order of depth, the nearer first: selects and not a branch.
void orderPuffs(inout KeptPuff front, inout KeptPuff back)
{
    const bool nearer = back.mAt < front.mAt;
    const KeptPuff was = front;
    front.mLight = nearer ? back.mLight : was.mLight;
    front.mAt = nearer ? back.mAt : was.mAt;
    back.mLight = nearer ? was.mLight : back.mLight;
    back.mAt = nearer ? was.mAt : back.mAt;
}

/// `layers` with a puff of `light`, already multiplied by `alpha`, met `at` along the ray: sorted in
/// from the back, and the two farthest merged — the nearer over the farther, as a blend in order
/// would draw them, at the depth their shares of the light weigh.
void addPuff(inout PuffLayers layers, vec3 light, float alpha, float at)
{
    layers.mIncoming = KeptPuff(vec4(light, alpha), at);
    orderPuffs(layers.mFarthest, layers.mIncoming);
    orderPuffs(layers.mThird, layers.mFarthest);
    orderPuffs(layers.mSecond, layers.mThird);
    orderPuffs(layers.mNearest, layers.mSecond);

    const vec4 front = layers.mFarthest.mLight;
    const vec4 back = layers.mIncoming.mLight;
    const float frontShare = front.a;
    const float backShare = (1.0 - front.a) * back.a;
    const float shares = frontShare + backShare;
    layers.mFarthest.mAt = shares > 0.0
        ? (layers.mFarthest.mAt * frontShare + layers.mIncoming.mAt * backShare) / shares
        : layers.mFarthest.mAt;
    layers.mFarthest.mLight = vec4(front.rgb + (1.0 - front.a) * back.rgb, front.a + (1.0 - front.a) * back.a);
    layers.mIncoming = noPuff();
}

/// The kept puffs composited front to back as far as one: the light in order under what the ones
/// before it left, what that leaves through, and the coverage and the depth at each one's share of
/// the light.
struct PuffsInOrder
{
    vec3 mLight;
    float mThrough;
    float mShown;
    float mShownAt;
};

/// `inOrder` with `kept` behind it.
void layPuffBehind(inout PuffsInOrder inOrder, KeptPuff kept)
{
    inOrder.mLight += inOrder.mThrough * kept.mLight.rgb;
    inOrder.mShown += inOrder.mThrough * kept.mLight.a;
    inOrder.mShownAt += inOrder.mThrough * kept.mLight.a * kept.mAt;
    inOrder.mThrough *= 1.0 - kept.mLight.a;
}

/// Every emitter's sprites the ray crosses, composited.
///
/// **No acceleration structure and one sphere per emitter.** A lamp is asked for by a shading *point*, which
/// the uniform grid answers in a lookup; an emitter is asked for by a whole *ray*, which would walk that grid
/// cell by cell. There are tens of emitters in a cell and each is small, so one rejection throws an emitter
/// away for almost every pixel of the frame.
///
/// **The covering ones in order, the nearest few of them**: `osgParticle` keeps its array in birth order and
/// sorting tens of sprites per pixel is not affordable, so a walk keeps the nearest four apart and merges the
/// rest behind them (`PuffLayers`), and the coverage it reports, `1 - prod(1 - a)`, is exact.
PuffLayer spritesAlong(uvec2 pixel, vec3 origin, vec3 direction, float limit, Cone cone, bool lit)
{
    PuffLayer layer = noPuffs();

    // **The tiles are derived and not carried**, from the same function the bin uses, so the two
    // cannot disagree about how many there are across.
    const uint tile = spriteTileOf(pixel, frame.mEyes.mWorld.mWidth);

    // **Every sprite where the tile's run did not fit**, which is the march as it was before the
    // tiles: `SPRITE_TILE_UNBINNED` says when a tile is handed it. The run is then every index in
    // turn, so a slot names its sprite directly.
    const SpriteRun run = spriteRunOf(SpriteTileList(frame.mTables.mSpriteTileList), tile);
    const bool unbinned = run.mUnbinned;
    uint slot = run.mSlot;
    const uint last = run.mEnd;

    // **Before anything is worked out for the walk.** A frame with no sprite in it, and a tile with none,
    // is most of the game, and what follows is a normalised cross, an exponential and a phase function that
    // a walk of nothing has no use for. Uniform over a tile, so a warp leaves whole.
    if (slot >= last)
        return layer;

    PuffLayers layers = noPuffLayers();
    float coverage = 0.0;
    vec3 addedThrough = vec3(1.0);

    // The disc's own axes, square to the ray and turned by the screen's up, for reading a sprite's texture
    // across the disc the ray sees. Hoisted because they are the ray's and not the sprite's.
    //
    // **Square to the ray and not the screen's own axes, because the disc is.** `ballCrossing` cuts the disc
    // square to the ray and the texture offset is measured along these: along the screen's axes the offset
    // lost its share along the ray's slant, so at the edge of a frame ninety degrees wide the silhouette
    // reached only seven tenths of the way across the texture and the blob's alpha drew a hard rim on every
    // puff off-centre. The cross is unit as long as the ray is not the screen's up, which no pinhole's ray
    // inside its own field of view is.
    const vec3 across = normalize(cross(direction, frame.mEyes.mWorld.mBasis.mUp));
    const vec3 upward = cross(across, direction);

    // The air along this one ray, built before the walk: every sprite below asks the same column
    // for a different distance, and what does not depend on the distance is an exponential.
    const FogRay air = fogRayFrom(origin, direction);

    // **Per emitter and not per sprite, across a walk with no emitter loop.** The tile's sprites are in
    // ascending index, and a sprite's index is contiguous within its emitter, so an emitter's sprites
    // arrive consecutively and these are read once for each run — off the row `spriteemitters.rgen` wrote
    // for the emitter, which is where what does not vary across it was worked out once for the whole frame.
    uint held = ~0u;
    GpuEmitter emitter;
    GpuEmitterFrame measured;
    bool missed = true;
    bool oriented = false;
    float width = 0.0;

    const vec3 toSun = frame.mSun.mDirection;

    // **The sun's share thrown forward, which is one angle for the whole ray.** A directional source holds
    // its angle to a straight ray, so the phase function is one evaluation for every sprite on it — the
    // same argument `fogVolumeAlong` makes, and the reason a shape this costly is affordable at all.
    const float thrownForward = smokeThrow(direction);

    for (; slot < last; ++slot)
    {
        const GpuSprite sprite = spriteAt(unbinned ? slot : spriteTileListAt(slot));

        if (sprite.mEmitter != held)
        {
            held = sprite.mEmitter;
            emitter = emitterAt(held);

            const vec3 toCentre = emitter.mCentre - origin;
            const float along = dot(toCentre, direction);

            // The same two rejections the emitter loop made, kept because a tile is sixteen pixels
            // wide and a sprite in it is one *some* ray of the tile can reach rather than this one.
            missed = (lit && (emitter.mFlags & EMITTER_ADDITIVE) != 0u) || along + emitter.mReach <= 0.0
                || along - emitter.mReach >= limit
                || dot(toCentre, toCentre) - along * along > emitter.mReach * emitter.mReach;

            // **An unbinned walk steps past a missed emitter's whole run**: its slot is the sprite's index,
            // met at the run's first, and the run lies end to end (`GpuEmitter::mFirst`), so a storm's
            // twenty emitters are twenty loads where its two thousand drops were.
            slot = unbinned && missed ? emitter.mFirst + emitter.mCount - 1u : slot;

            if (!missed)
            {
                measured = emitterFrameAt(held);

                // **A width of nothing is a sprite that faces the eye**, which is nearly every
                // emitter in the game; asked once for the emitter rather than once for each of its
                // sprites. `fixed` is a reserved word in GLSL, which is why this is not called one.
                width = emitter.mWidth;
                oriented = width > 0.0;
            }
        }

        if (missed)
            continue;

        const vec3 toSprite = sprite.mPosition - origin;

        // The only place the two kinds of sprite differ. An `if` and not a select, so the kind the
        // emitter is not costs nothing.
        SpriteCrossing crossing;
        if (oriented)
            crossing = quadCrossing(sprite, toSprite, direction, limit, width, measured.mTexels);
        else
            crossing = ballCrossing(sprite, toSprite, direction, limit, across, upward, measured.mTexels,
                (emitter.mFlags & EMITTER_FIRST_PERSON) != 0u);

        if (!crossing.mFound)
            continue;

        // How many texels the pixel's cone covers where the sprite stands, which is the level that resolves
        // it: the cone has spread to `mWidth + mSpread * seen` there, and `rate` is what the sprite carries
        // per unit of that.
        const float lod = log2(max(crossing.mRate * (cone.mWidth + cone.mSpread * crossing.mSeen), 1.0));

        // The quad `osgParticle` would have drawn: texture coordinate zero at `-right -up` and
        // one at `+right +up`, about a centre at half.
        const vec2 uv = crossing.mAt * 0.5 + 0.5;

        const vec4 texel = textureLod(TEXTURE(sceneTexels(), emitter.mTexture), uv, lod);

        // **The rim is put back on a disc and left alone on a quad.** What the taper restores is a round
        // blob the mip chain averaged into the square it was cut to; a rain streak is authored as that
        // rectangle, and tapering it would round off the drop. `ONE, ONE` reads no alpha, as `gatherAlong`
        // reads none of a surface that adds whole.
        const float read = (emitter.mFlags & EMITTER_ADD_WHOLE) != 0u ? 1.0 : texel.a;
        const float painted = read * sprite.mAlpha * (oriented ? 1.0 : spriteTaper(crossing.mRadial, lod));
        if (!(painted > 0.0))
            continue;

        // What the eye's share of the chord hides, which is `paintedOver`'s own law: the whole of
        // what was painted for a whole chord, and less for part of one. A streak is whole by
        // construction and takes no power, on a test that is the emitter's.
        const float alpha = oriented ? paintedWhole(painted) : paintedOver(painted, crossing.mFraction);
        const vec3 colour = texel.rgb * sprite.mColour;

        if ((emitter.mFlags & EMITTER_ADDITIVE) != 0u)
        {
            // **The layer taken exactly and the band taken once.** A sheet of sprites and the wall behind
            // it are one distance from the eye and were fading at two rates: the wall goes through the
            // volume, which integrates the height falloff, and these charged one density over the whole
            // path — so a puff seen down a slope kept a third more of itself than the air left it.
            const float reaching = exp(-fogColumnOver(air, crossing.mSeen) * measured.mBand);

            // **No gain, deliberately.** The blend the file asks for says how much light the sprite adds;
            // `SUNLIT_WHITE` only carries the original's scale, where a fully lit surface reached one, onto this
            // renderer's. A flame comes out tens of times the mean of its room because that is what a flame is,
            // and the exposure downstream decides where it lands.
            //
            // **And it absorbs as much as it emits, per channel**, which makes the screen in the accumulator
            // exact for a stack: what one sprite adds is what it always added, and twenty saturate at the white
            // the original's framebuffer clamped to, not at twenty times it.

            const vec3 glow
                = (oriented ? paintedWhole(colour * painted) : paintedOver(colour * painted, crossing.mFraction))
                * reaching;
            addedThrough *= 1.0 - glow;

            continue;
        }

        coverage += alpha;
        layer.mTransmittance *= 1.0 - alpha;

        if (!lit)
        {
            addPuff(layers, colour * alpha, alpha, crossing.mSeen);
            continue;
        }

        const float reaching = exp(-fogColumnOver(air, crossing.mSeen) * measured.mBand);

        // What this puff's own shape leaves of what the air around it is lit by: the ball's own
        // side and what its texture lets through to this texel, or the cylinder a streak is drawn
        // as.
        PuffShape wrapped;

        if (oriented)
        {
            wrapped = streakPuff(crossing.mAlong);
        }
        else
        {
            // Where the ray entered the ball, as a normal: `at` across the disc, and the ball's
            // surface lifted toward the eye by what is left of the radius there.
            const vec3 normal = normalize(across * crossing.mAt.x + upward * crossing.mAt.y - direction * crossing.mLift);

            wrapped = ballPuff(normal, thrownForward);

            if (holdsTexture(emitter.mLighting))
            {
                // **What the puff's own texture leaves of each light**, thinned as the puff's own
                // fade thins it: a wisp near the end of its life shadows itself less than the puff
                // it was. The power is the same one the bake takes per texel, applied to the whole.
                const vec4 shade
                    = pow(textureLod(TEXTURE(sceneTexels(), emitter.mLighting), uv, lod), vec4(sprite.mAlpha));
                const float back = 1.0 - texel.a * sprite.mAlpha;
                const vec3 facing = -direction;

                wrapped.mSunLit *= sixWayThrough(toSun, across, upward, facing, shade, back);

                // **The mean over all six ways in for the term with no direction in it**, which is
                // what the room's fill always took: an ambient that arrives from everywhere is let
                // through by the whole of the bake rather than by one of its faces.
                wrapped.mAmbientLit *= (shade.x + shade.y + shade.z + shade.w + 1.0 + back) / 6.0;
            }

            // **What the rest of its own emitter leaves of the light**, as the layers of sprites between
            // this one and the sun and the sky — counted by `spriteshade.comp` — thinned here by what one
            // layer of this texture hides on average.
            wrapped.mSunLit *= exp2(measured.mLayerThrough * sprite.mSunLayers);
            wrapped.mAmbientLit *= exp2(measured.mLayerThrough * sprite.mSkyLayers);
        }

        addPuff(layers, colour * puffLight(pixel, direction, crossing.mSeen, wrapped) * (alpha * reaching), alpha,
            crossing.mSeen);
    }

    // **Front to back**: each layer's light under what the ones before it left, and the depth at the share
    // of the light each gave.
    PuffsInOrder inOrder = PuffsInOrder(vec3(0.0), 1.0, 0.0, 0.0);
    layPuffBehind(inOrder, layers.mNearest);
    layPuffBehind(inOrder, layers.mSecond);
    layPuffBehind(inOrder, layers.mThird);
    layPuffBehind(inOrder, layers.mFarthest);

    if (coverage > 0.0)
    {
        layer.mColour = inOrder.mLight / max(inOrder.mShown, 1.0e-12);
        layer.mCoveredAt = inOrder.mShownAt / max(inOrder.mShown, 1.0e-12);
        layer.mWeight = coverage;
    }

    layer.mAdded = (1.0 - addedThrough) * SUNLIT_WHITE;

    return layer;
}

/// Two layers of puffs as one: the sprites' and a medium's.
///
/// **No order between them, so weighed by the sum of each layer's alphas**, the rule a medium's walk uses
/// inside itself.
///
/// **What it gives up is the depth**, and only where both walks found something on one pixel: rain a few
/// units out and a cloud two thousand away come to one mean the air is split at.
PuffLayer mergedPuffs(PuffLayer first, PuffLayer second)
{
    const float weight = first.mWeight + second.mWeight;

    PuffLayer layer;
    layer.mAdded = first.mAdded + second.mAdded;
    layer.mTransmittance = first.mTransmittance * second.mTransmittance;
    layer.mWeight = weight;
    layer.mColour
        = weight > 0.0 ? (first.mColour * first.mWeight + second.mColour * second.mWeight) / weight : vec3(0.0);
    layer.mCoveredAt
        = weight > 0.0 ? (first.mCoveredAt * first.mWeight + second.mCoveredAt * second.mWeight) / weight : 0.0;

    return layer;
}

#endif
