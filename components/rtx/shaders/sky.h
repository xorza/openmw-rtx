#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SKY_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SKY_H

#include "hosttypes.h"
#include "portable.h"

// What the game says is over the world: the weather it is, the deck and the sheets that are drawn,
// the discs that are drawn and light, and the gradient behind all of them.
//
// **Apart from `visibility.h` because two passes want this and not the frame.** The tone pass draws
// the stars and the fog's set names one layer a source, and neither wants the whole frame
// description — thirteen hundred bytes and a 64-bit extension — for one struct.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Morrowind's cloud deck, as a ray that reached nothing finds it.
    ///
    /// **A layer at a height rather than the dome the game shipped.** The engine hangs its clouds on
    /// a mesh whose UVs were painted into the file, which is a thing to rasterize and not a thing to
    /// intersect; what that mesh is *for* is a layer of cloud seen in perspective, and a ray tracer
    /// can have the layer itself. Everything here is the game's own, the shape of that layer
    /// included: `CloudShell` is where its height and its curvature are read off the mesh.
    struct CloudDeck
    {
        /// How much deck there is, from none of it to all.
        ///
        /// **Nought is no sky at all, and it is nought by default.** A `VisibilityConstants` is a
        /// plain C structure shared with the shader and has no constructor to run, so whatever means
        /// "there is nothing here" has to be what zeroing it says — a texture index cannot, because
        /// zero is a real slot and every frame that forgot to say otherwise would draw slot nought
        /// across its whole sky. `StarField::mFade` is the same field for the same reason.
        float mOpacity;

        /// What a cloud in full sunlight radiates from below, linear.
        ///
        /// **The sky, the moons and the sun, each spread over the underside of the layer**, and
        /// `Rtx::deckLight` is where the three are added. `CLOUD_TRANSMISSION` is what a deck keeps
        /// of them.
        vec3 mLit RTX_ZERO;

        /// What a cloud in its own shadow radiates: the sky alone.
        ///
        /// **The light with no direction is the light a cloud cannot shadow itself from.** A deck's
        /// own body is what keeps the sun off its base, so the sheet's paint picks between this and
        /// `mLit` — and at night, with no sun over the layer, the two differ only by the moons.
        vec3 mShadowed RTX_ZERO;

        /// The mean luminance of what the sheets being sampled paint, linear.
        ///
        /// **What a texel is read as a ratio to, so the sheet gives shape and `mColour` gives the
        /// level.** `CloudSheet::mMean` carries the argument and the measurements.
        ///
        /// Nought where the sheet could not be averaged — a file a mod replaced with something
        /// nothing here decodes — which the shader reads as no ratio to take, and draws the deck
        /// flat as it did before it read the paint at all.
        float mMean RTX_ZERO;

        /// The mean alpha of the sheets being sampled: how much sky the deck hides on average.
        ///
        /// **What a shadow is measured against**, so that darkening the ground states the pattern
        /// and not the weather — `CLOUD_SHADOW_DEPTH` carries the argument.
        float mCover RTX_ZERO;

        /// Where the layer stands, as a world height, and how many tiles of its sheet one world unit
        /// is along each axis.
        ///
        /// **Signed, because the mesh's own unwrap is.** `CloudShell::mTiles` comes off the cloud
        /// mesh with its `v` axis running the other way, and dropping that sign mirrors every
        /// sheet.
        ///
        /// **The one number in the sky that is chosen rather than read**, and `Rtx::sCloudAltitude`
        /// says so: the mesh gives its height in tiles of its own sheet and no metre anywhere. It is
        /// what lets the sheet be addressed from where the eye stands rather than from where it
        /// looks, and so what lets the deck cast.
        float mAltitude;
        vec2 mPerTile;

        /// How far from `mTexture` to `mNext`. A settled sky names the same texture twice at zero,
        /// and a weather with no sheet ahead names the near one twice on its own bearing, so the
        /// shader mixes unconditionally rather than testing for a transition.
        float mBlend RTX_ZERO;

        /// The scroll along `v`, in texture widths. `SkyReader` advances it, off the clouds' own
        /// clock.
        float mScroll RTX_ZERO;

        /// Which way each of the two sheets is driven, as a unit bearing in the ground plane.
        ///
        /// **The storm's own direction with its two components swapped, and no angle in between.**
        /// The engine turns each cloud mesh from due north onto that direction, and turning a
        /// crossing by the same angle wants the cosine and the sine of it — which for a unit `(x,
        /// y)` measured from north is `(y, x)`. Reaching that pair through `atan2` and back through
        /// `sin` and `cos` costs three transcendentals a sample and arrives at the same place.
        ///
        /// **One each, because the engine turns each mesh by its own weather's storm.** A
        /// transition into an ashstorm drives the sheet ahead off Red Mountain while the one
        /// overhead still runs due north.
        vec2 mBearing RTX_ZERO;
        vec2 mNextBearing RTX_ZERO;

        /// How far the layer falls away over the ground it covers, and the three crossing radii the
        /// engine's own fade turns on. `CloudShell` holds what each of them means and why neither is
        /// a constant.
        float mCurvature RTX_ZERO;
        vec3 mRings RTX_ZERO;

        uint mTexture;
        uint mNext;
    };

    /// How many patches the night sky is painted with, over and above the star field.
    const uint SKY_PATCH_COUNT = 6u;

    /// One of them, as a ray that reached nothing finds it.
    ///
    /// **The same thing a moon is**, and drawn the same way: a direction, an angular size, and a
    /// sheet laid across the face. `describePatches` (`skybuilder.hpp`) says where the six are and
    /// how big, measured off the mesh the rasterizer hangs them on.
    struct SkyPatch
    {
        vec3 mDirection;

        /// The face's own axes, unit and square to `mDirection` and to each other.
        vec3 mRight;
        vec3 mUp;

        /// The sine of half the angle it subtends, which is how far off the centre line a
        /// direction at the limb stands. The nebulae reach past a radian, which is why they read as
        /// a tint over the sky rather than as something in it.
        ///
        /// **The sine and not the angle**, for the reason `CloudDeck::mBearing` gives: every reader
        /// wants it through `sin`, and a ray is not the place to take the sine of a number that is
        /// the same for the whole frame. The host has the angle and keeps it.
        float mLimb;

        uint mTexture;
    };

    /// The star field.
    ///
    /// **Stars are on a sphere and clouds are on a plane**, which is the whole difference between
    /// this and `CloudDeck`: a cloud layer converges at the horizon and a star does not move as the
    /// eye does. The sheet is laid on that sphere at the scale the engine's own mesh lays it at.
    struct StarField
    {
        /// How much of the sheet is there: the engine's `Stars` ramp times the weather's glare, so
        /// stars come out at dusk and an overcast keeps them in — and times the day's gain, which
        /// `Rtx::describeWorld` lifts the whole sky by, so past one while a dusk is still lifted.
        float mFade;

        /// What every sheet of the night sky adds to what the sky *lights* with, already faded.
        ///
        /// **The sheets as a source rather than as a picture**, and the two are reached differently:
        /// a ray that is looked along samples them where it points, and one gathering a hemisphere
        /// takes this instead. `NightSky::mGlow` says why a mean and not the sheets themselves.
        vec3 mGlow RTX_ZERO;

        /// How far the sphere has rolled about the zenith, in radians. Once every four days.
        float mTurn;

        /// How much sky one tile of the sheet covers, in radians — **read off the mesh** rather than
        /// chosen, and it is what decides how big a star is. `Rtx::NightSky` measures it as the
        /// median rate the unwrap runs at, and the unwrap is isotropic, which is what keeps a star
        /// round. Morrowind's comes to about a tenth of a degree per texel; the same sheet spread
        /// once over the hemisphere would be a third, which is a blob.
        float mTile;

        /// The elevation the field fades out below, in radians. The mesh's again: the engine draws a
        /// vertex of that dome only where its authored colour is white, and its bottom ring is not.
        float mHorizon;

        uint mTexture;
    };

    /// One source in the sky as a shading point sees it: the sun, or a moon. What the eye sees of
    /// a disc is `MoonDisc`'s and the sun's own field; this is the half that lights.
    ///
    /// **Three of them and one rule.** A surface and a froxel of the air weigh each by what it would
    /// deliver unshadowed, draw one, trace to it and divide by the draw — the lamps' own estimator.
    /// In daylight the moons weigh nothing and the sun is always drawn; at night the sun weighs
    /// nothing and the draw is between the moons; and the hour either side of dusk spends one ray
    /// and carries the noise. `skySourceAt` in `lib/lights.glsl` is where the three are read off
    /// the frame.
    struct SkySource
    {
        /// Unit, from a point toward the source.
        vec3 mDirection;

        /// Nought where the source is down or faded out, which is the one test worth making before
        /// a ray.
        vec3 mIrradiance;

        /// The sine of the half angle a shadow ray is drawn across: the sun's `SUN_SHADOW_RADIUS`,
        /// and a moon's own limb.
        float mLimb;
    };

    const uint SKY_SOURCE_SUN = 0u;
    const uint SKY_SOURCE_MASSER = 1u;
    const uint SKY_SOURCE_SECUNDA = 2u;

    /// One of the two moons, as a disc a ray that reached nothing can find.
    ///
    /// **A disc and not a body**, for the reason the sun is: nothing puts a sphere in an
    /// acceleration structure, so a moon is a direction with a size and a face painted across it.
    /// What that buys is the same thing the sun's disc buys — water traces a reflection ray and
    /// finds the moon in it for nothing, and there is one place a moon's size lives.
    struct MoonDisc
    {
        /// The moon as a light: unit toward it, what it delivers to a surface facing it, and the
        /// sine of half the angle its disc subtends.
        ///
        /// **The record itself and not its three fields**, so `skySourceAt` and the fog read one
        /// field where they assembled a source from three. The direction is the disc's too: the
        /// face turns against the horizon as the moon crosses, which is what a tidally locked moon
        /// does and what a billboard does not.
        ///
        /// **A light and the disc are two numbers here, not one.** `Shaders::MOON_ALBEDO` says why
        /// the level a moon lights by cannot be read off the radiance it is drawn at. A zero
        /// irradiance is a moon that lights nothing, and it is the one test worth making before a
        /// shadow ray.
        ///
        /// The limb is the sine and not the angle, for the reason `SkyPatch::mLimb` gives.
        /// Masser's angle is between five and a half degrees and nine and a half, on the two
        /// `Moons_Masser_Size` the game ships — twenty to thirty-six times the sun either way.
        SkySource mSource;

        /// The two axes the face is painted along.
        vec3 mRight;
        vec3 mUp;

        /// What a fully lit face sends back, linear.
        vec3 mColour;

        /// Which way the light falls on the face, in the face's own frame — along `mRight`, along
        /// `mUp`, and toward the eye — unit: straight out of the face at full, and edge-on at a half.
        ///
        /// **The share that is lit comes from the game and the direction it faces comes from the
        /// sky.** Morrowind advances a phase on its own three-day clock, which owes nothing to where
        /// its sun actually is — so the terminator is carved at the angle the game names and then
        /// turned so the lit limb points at the sun, which is the only orientation that does not
        /// read as a mistake.
        ///
        /// **Worked out once for the frame and not at every pixel of the disc**, because both halves
        /// are the frame's: `describeMoon` puts them together.
        vec3 mLitFrom;

        /// How much of McEwen's lunar-Lambert the face is shaded by at its phase, against Lambert's
        /// own law: one at full, and nought from the phase McEwen's fit reaches nought at. A phase
        /// is the frame's, so this is too.
        float mLunar;

        /// What the game fades the moon by near the horizon and at the ends of its arc. Zero is a
        /// moon that is not there, and the whole disc is skipped for it.
        float mAlpha;

        /// What the air leaves of it, per channel.
        ///
        /// **What a moon is dimmed by on the way up, in place of being switched off.** The engine
        /// draws none under `Moons_<name>_Fade_End_Angle`; here the slant path through the air does
        /// that and does it from the horizon, so a moon comes over the edge as a deep red ember.
        /// `Rtx::airTransmittance` carries the two published figures it is made of.
        ///
        /// **It dims and never uncovers.** What stands behind a moon is hidden by `mAlpha` alone,
        /// because a moon low in the air still blocks a star — what replaces it there is the airlight
        /// in front, which is the dome, and the dome is added over the whole sky anyway.
        vec3 mThroughAir;

        /// What a script painted the face, linear — `Rtx::MoonPlacement::mPaint`. White for a moon
        /// nobody painted. Over the face here, and already over `mIrradiance`.
        vec3 mPaint;

        /// The painted face, in the bindless array, or `NO_TEXTURE` where none was loaded — the disc
        /// is then its mean colour with the shading law over it.
        ///
        /// **The `full` portrait and only that one.** The game ships eight per moon and this draws
        /// the terminator itself, so what is wanted from the file is the maria and the silhouette —
        /// one face under eight lightings, which is what a tidally locked moon is.
        ///
        /// **The alpha is not premultiplied.** Past the edge of the painted disc the file's colour
        /// climbs back toward the middle of its range, so a sampler that takes the colour and drops
        /// the alpha draws a bright ring around every moon. Multiplying by it removes that and hands
        /// over the limb's own antialiasing for nothing.
        uint mFace;
    };

    /// How many of them there are, in `SkySource` order after the sun: Masser, then Secunda.
    const uint MOON_COUNT = 2u;

#ifdef RTX_HOST
    /// A moon as the frame carries it, from the angle its disc subtends: the host's one spelling
    /// of `MoonDisc::mSource`, beside `sunSource` for the sun, so a moon assembled by hand cannot
    /// leave its limb at nought and cast a hard edge.
    inline SkySource moonSource(const vec3& direction, const vec3& irradiance, float angularRadius)
    {
        return SkySource{ direction, irradiance, std::sin(angularRadius) };
    }
#endif

    /// Where Morrowind's atmosphere fades the fog colour to the sky colour, read off the mesh the
    /// rasterizer draws it with (`Rtx::readAtmosphere`). The mesh is a cylinder of two rings: the sky
    /// colour whole on the upper ring and none on the lower, linear between them along the wall, and
    /// fanned shut above the upper ring. A ray at elevation `e` crosses the wall at the share
    /// `(r0 sin e - z0 cos e) / ((z1 - z0) cos e - (r1 - r0) sin e)` of the way up it, which is the
    /// rasterizer's interpolation in a vertical plane through a ring vertex: in Morrowind's own mesh,
    /// the whole fade between 3.6 and 28.6 degrees.
    ///
    /// **All nought is a frame built by hand, and draws the sky colour above the horizon.** No
    /// atmosphere at all is `mBottom` and `mTop` above one, which draws the fog colour everywhere,
    /// as the rasterizer does with no mesh to draw.
    struct SkyRamp
    {
        /// The sines of the two rings' elevations: none of the sky colour below the first, all of
        /// it above the second.
        float mBottom;
        float mTop;

        /// The lower ring's radius and height, `r0, z0`, and the upper ring's less those,
        /// `r1 - r0, z1 - z0`. A ratio is all the share reads, so the mesh's own units stand.
        vec2 mLow RTX_ZERO;
        vec2 mStep RTX_ZERO;
    };

    /// How much of the sky colour the atmosphere shows along unit `direction`: `SkyRamp`'s share.
    ///
    /// **Selected and not branched.** The division answers only between the rings, where the wall
    /// is crossed and what it divides by is positive; outside them it may divide by nought, and
    /// that answer is not the one selected.
    RTX_SHADER float skyShare(SkyRamp ramp, vec3 direction)
    {
        const float up = direction[2];
        const float across = sqrt(max(direction[0] * direction[0] + direction[1] * direction[1], 0.0f));
        const float rise = ramp.mLow[0] * up - ramp.mLow[1] * across;
        const float run = ramp.mStep[1] * across - ramp.mStep[0] * up;
        const float between = clamp(rise / run, 0.0f, 1.0f);
        return up >= ramp.mTop ? 1.0f : (up <= ramp.mBottom ? 0.0f : between);
    }

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(MoonDisc) == 112, "MoonDisc must be scalar-packed on every side");
    static_assert(sizeof(CloudDeck) == 96, "CloudDeck must be scalar-packed on every side");
    static_assert(sizeof(StarField) == 32, "StarField must be scalar-packed on every side");
    static_assert(sizeof(SkyPatch) == 44, "SkyPatch must be scalar-packed on every side");
    static_assert(sizeof(SkyRamp) == 24, "SkyRamp must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

// What the shading language reads and the host does not, for the reason `RTX_SHADER` gives.
#ifndef RTX_HOST

/// The sky's own colour along a direction: the game's fog colour faded to its sky colour by the
/// atmosphere, `SkyRamp`.
///
/// **The two colours and the ramp rather than the frame they sit in.** What this is about is a
/// gradient between two colours, and a function that took the frame would tie itself to how a
/// backend binds one.
///
/// Morrowind clears to the fog colour and draws its atmosphere over it, so a ray that reaches
/// nothing under the atmosphere's lower ring converges on exactly what a ray through a mile of air
/// does.
RTX_SHADER vec3 skyGradient(vec3 horizon, vec3 zenith, SkyRamp ramp, vec3 direction)
{
    return mix(horizon, zenith, skyShare(ramp, direction));
}

#endif

#endif
