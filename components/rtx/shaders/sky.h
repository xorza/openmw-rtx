#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SKY_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SKY_H

#include "hosttypes.h"
#include "portable.h"

// What the game says is over the world: the weather it is, the deck and the sheets that are drawn,
// the discs that are drawn and light, and the gradient behind all of them. **Apart from
// `visibility.h`** because the tone pass and the fog's set want these structs and not the whole frame
// description.

#ifdef RTX_HOST

#include <cmath>

namespace Rtx::Shaders
{
#endif

    /// Morrowind's cloud deck, as a ray that reached nothing finds it. **A layer at a height rather
    /// than the dome the game shipped**: that mesh has UVs painted into the file, which suits
    /// rasterizing and not intersecting. `CloudShell` reads its height and curvature off the mesh.
    struct CloudDeck
    {
        /// How much deck there is, from none of it to all. **Nought is no sky at all, and it is
        /// nought by default**: a `VisibilityConstants` is a plain C structure with no constructor, so
        /// "nothing here" has to be what zeroing says, and a texture index cannot (zero is a real
        /// slot). `StarField::mFade` is the same field for the same reason.
        float mOpacity;

        /// What a cloud in full sunlight radiates from below, linear: the sky, the moons and the sun,
        /// each spread over the layer's underside (`Rtx::deckLight` adds the three;
        /// `CLOUD_TRANSMISSION` is what a deck keeps of them).
        vec3 mLit RTX_ZERO;

        /// What a cloud in its own shadow radiates: the sky alone, the light a cloud cannot shadow
        /// itself from. The sheet's paint picks between this and `mLit`; at night, with no sun, the two
        /// differ only by the moons.
        vec3 mShadowed RTX_ZERO;

        /// The mean luminance of what the sheets being sampled paint, linear: **what a texel is read as
        /// a ratio to, so the sheet gives shape and `mLit` and `mShadowed` the level**
        /// (`CloudSheet::mMean`). Nought where the sheet could not be averaged (a file a mod replaced
        /// with something undecodable), which the shader reads as no ratio, drawing the deck flat.
        float mMean RTX_ZERO;

        /// Where the layer stands, as a world height, and how many tiles of its sheet one world unit
        /// is along each axis. **Signed, because the mesh's own unwrap is**: `CloudShell::mTiles` has
        /// its `v` axis running the other way, and dropping that sign mirrors every sheet.
        /// **The one number in the sky that is chosen rather than read** (`Rtx::sCloudAltitude`): the
        /// mesh gives its height in tiles of its own sheet and no metre anywhere.
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
        /// **The storm's own direction with its two components swapped, and no angle in between**:
        /// the engine turns each mesh from due north onto that direction, and turning a crossing by
        /// the same angle wants cosine and sine, which for a unit `(x, y)` from north is `(y, x)`;
        /// going through `atan2` and back costs three transcendentals a sample. **One each**, because
        /// the engine turns each mesh by its own weather's storm: a transition into an ashstorm drives
        /// the sheet ahead off Red Mountain while the one overhead still runs due north.
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

    /// One of the night sky's patches, as a ray that reached nothing finds it: **the same thing a
    /// moon is**, a direction, an angular size and a sheet laid across the face. `describePatches`
    /// (`skycontent.hpp`) says where the six are and how big, measured off the mesh the rasterizer
    /// hangs them on.
    struct SkyPatch
    {
        vec3 mDirection;

        /// The face's own axes, unit and square to `mDirection` and to each other.
        vec3 mRight;
        vec3 mUp;

        /// The sine of half the angle it subtends: how far off the centre line a direction at the limb
        /// stands. The nebulae reach past a radian, so they read as a tint over the sky. **The sine
        /// and not the angle**, as `CloudDeck::mBearing` does: every reader wants it through `sin` and
        /// a ray is no place to take the sine of a frame-constant.
        float mLimb;

        uint mTexture;
    };

    /// The star field. **Stars are on a sphere and clouds are on a plane**: a cloud layer converges
    /// at the horizon and a star does not move as the eye does. The sheet is laid on that sphere at
    /// the scale the engine's own mesh lays it at.
    struct StarField
    {
        /// How much of the sheet is there: the engine's `Stars` ramp times the weather's glare, so
        /// stars come out at dusk and an overcast keeps them in — and times the day's gain, which
        /// `Rtx::describeWorld` lifts the whole sky by, so past one while a dusk is still lifted.
        float mFade;

        /// What every sheet of the night sky adds to what the sky *lights* with, already faded. **The
        /// sheets as a source rather than as a picture**: a ray looked along samples them where it
        /// points, one gathering a hemisphere takes this instead (`NightSky::mGlow` says why a mean).
        vec3 mGlow RTX_ZERO;

        /// How far the sphere has rolled about the zenith, in radians. Once every four days.
        float mTurn;

        /// How much sky one tile of the sheet covers, in radians — **read off the mesh** rather than
        /// chosen, and what decides how big a star is. `Rtx::NightSky` measures the median rate the
        /// unwrap runs at, which is isotropic, so a star stays round. Morrowind's is about a tenth of a
        /// degree per texel; spread once over the hemisphere a star would be a blob.
        float mTile;

        /// The elevation the field fades out below, in radians. The mesh's again: the engine draws a
        /// vertex of that dome only where its authored colour is white, and its bottom ring is not.
        float mHorizon;

        uint mTexture;

        /// How many texels the sheet `mTexture` names stands at, across and down. **The backend's and
        /// not the scene's**: the array can stand a texture smaller than its file
        /// (`TextureArray::chooseLevels`). Written beside the ripples, so a starry pixel reads a
        /// number the host holds rather than a texture header.
        uvec2 mExtent RTX_ZERO;
    };

    /// One source in the sky as a shading point sees it: the sun, or a moon. What the eye sees of a
    /// disc is `MoonDisc`'s and the sun's own field; this is the half that lights.
    ///
    /// **Three of them and one rule.** A surface and a froxel weigh each by what it would deliver
    /// unshadowed, draw one, trace to it and divide by the draw, as the lamps do. In daylight the moons
    /// weigh nothing; at night the sun weighs nothing; around dusk one ray carries the noise.
    /// `skySourceAt` reads the three off the frame.
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

    /// One of the two moons, as a disc a ray that reached nothing can find. **A disc and not a body**,
    /// as the sun is: nothing puts a sphere in an acceleration structure, and water's reflection ray
    /// finds the moon for nothing, with one place a moon's size lives.
    struct MoonDisc
    {
        /// The moon as a light: unit toward it, what it delivers to a surface facing it, and the sine
        /// of half the angle its disc subtends (the sine for the reason `SkyPatch::mLimb` gives).
        /// **The record itself and not its three fields**, so `skySourceAt` and the fog read one field.
        /// The direction is the disc's too: the face turns against the horizon as the moon crosses, as
        /// a tidally locked moon does and a billboard does not. **A light and the disc are two numbers
        /// here**: `Shaders::MOON_ALBEDO` says why the level a moon lights by cannot be read off the
        /// radiance it is drawn at. A zero irradiance lights nothing, the one test worth making before a
        /// shadow ray.
        SkySource mSource;

        /// The two axes the face is painted along.
        vec3 mRight;
        vec3 mUp;

        /// What a fully lit face sends back, linear.
        vec3 mColour;

        /// Which way the light falls on the face, in the face's own frame — along `mRight`, along
        /// `mUp`, and toward the eye — unit: straight out of the face at full, edge-on at a half.
        /// **The lit share comes from the game and the direction it faces from the sky**: Morrowind's
        /// phase runs on its own three-day clock, owing nothing to where its sun is, so the terminator is
        /// carved at the angle the game names and turned so the lit limb points at the sun. Worked out
        /// once per frame (`describeMoon`), not at every pixel of the disc.
        vec3 mLitFrom;

        /// How much of McEwen's lunar-Lambert the face is shaded by at its phase, against Lambert's
        /// own law: one at full, and nought from the phase McEwen's fit reaches nought at. A phase
        /// is the frame's, so this is too.
        float mLunar;

        /// What the game fades the moon by near the horizon and at the ends of its arc. Zero is a
        /// moon that is not there, and the whole disc is skipped for it.
        float mAlpha;

        /// What the air leaves of it, per channel: **a moon dimmed on the way up in place of being
        /// switched off**. The engine draws none under `Moons_<name>_Fade_End_Angle`; here the slant
        /// path does it from the horizon, so a moon comes over the edge as a deep red ember
        /// (`Rtx::airTransmittance`). **It dims and never uncovers**: what stands behind a moon is
        /// hidden by `mAlpha` alone, as a low moon still blocks a star.
        vec3 mThroughAir;

        /// What a script painted the face, linear — `Rtx::MoonPlacement::mPaint`. White for a moon
        /// nobody painted. Over the face here, and already over `mIrradiance`.
        vec3 mPaint;

        /// The painted face, in the bindless array, or `NO_TEXTURE` where none was loaded — the disc is
        /// then its mean colour with the shading law over it. **The `full` portrait only**: the game
        /// ships eight per moon and this draws the terminator itself, wanting the maria and silhouette.
        /// **The alpha is not premultiplied**: past the painted disc the file's colour climbs back
        /// toward mid-range, so a sampler that drops the alpha draws a bright ring around every moon.
        /// Multiplying by it removes that and gives the limb's antialiasing for nothing.
        uint mFace;
    };

    /// How many of them there are, in `SkySource` order after the sun: Masser, then Secunda.
    const uint MOON_COUNT = 2u;

#ifdef RTX_HOST
    /// A source in the sky as the frame carries it, from the angle its disc subtends. The host's one
    /// spelling of a `SkySource` (the sun's `sunSource` and each moon's `MoonDisc::mSource`), so one
    /// assembled by hand cannot leave its limb at nought and cast a hard edge.
    inline SkySource skySource(const vec3& direction, const vec3& irradiance, float angularRadius)
    {
        return SkySource{ direction, irradiance, std::sin(angularRadius) };
    }
#endif

    /// Where Morrowind's atmosphere fades the fog colour to the sky colour, read off the mesh the
    /// rasterizer draws it with (`Rtx::readAtmosphere`): a cylinder of two rings, the sky colour whole
    /// on the upper and none on the lower, linear between along the wall. A ray at elevation `e`
    /// crosses the wall at the share `(r0 sin e - z0 cos e) / ((z1 - z0) cos e - (r1 - r0) sin e)` of
    /// the way up, the rasterizer's interpolation in a vertical plane through a ring vertex (in
    /// Morrowind's mesh, the fade between 3.6 and 28.6 degrees).
    ///
    /// **All nought is a frame built by hand, and draws the sky colour above the horizon.** No
    /// atmosphere at all is `mBottom` and `mTop` above one, which draws the fog colour everywhere, as
    /// the rasterizer does with no mesh to draw.
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
    /// **Selected and not branched**: the division answers only between the rings, where its divisor
    /// is positive; outside them it may divide by nought, and that answer is not the one selected.
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
    static_assert(sizeof(CloudDeck) == 92, "CloudDeck must be scalar-packed on every side");
    static_assert(sizeof(StarField) == 40, "StarField must be scalar-packed on every side");
    static_assert(sizeof(SkyPatch) == 44, "SkyPatch must be scalar-packed on every side");
    static_assert(sizeof(SkyRamp) == 24, "SkyRamp must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

// What the shading language reads and the host does not, for the reason `RTX_SHADER` gives.
#ifndef RTX_HOST

/// The sky's own colour along a direction: the game's fog colour faded to its sky colour by the
/// atmosphere, `SkyRamp`. **The two colours and the ramp rather than the frame they sit in**, so it
/// does not tie itself to how a backend binds one. Morrowind clears to the fog colour and draws its
/// atmosphere over it, so a ray under the lower ring converges on what a ray through a mile of air does.
RTX_SHADER vec3 skyGradient(vec3 horizon, vec3 zenith, SkyRamp ramp, vec3 direction)
{
    return mix(horizon, zenith, skyShare(ramp, direction));
}

#endif

#endif
