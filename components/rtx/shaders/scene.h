#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SCENE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SCENE_H

// The scene's tables, and the scale its brightnesses are measured on, as both sides see them.
// Scalar block layout throughout, so a `uint` is four bytes and a `vec2` is eight on both sides and
// there is nothing to translate.
//
// The constants are here for the same reason the structures are: a number one side derives and the
// other applies has to be one number. Two files each holding their own copy is how a sun and a lamp
// quietly stop being on the same scale.

#ifdef __cplusplus

#include <cstdint>

#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

namespace Rtx::Shaders
{
    using vec2 = osg::Vec2f;
    using vec3 = osg::Vec3f;
    using vec4 = osg::Vec4f;
    using uint = std::uint32_t;

#endif

    /// A material with no texture in a slot stores this.
    const uint NO_TEXTURE = 0xFFFFFFFFu;

    /// How many sinusoids the water surface is summed from.
    const uint WAVE_COUNT = 32u;

    /// A whole turn, which is how a wavelength becomes a wavenumber.
    const float TAU = 6.2831853f;

    /// Morrowind's gravity, in world units per second squared: 8.96 m/s^2 across 69.99 units to
    /// the metre.
    const float WATER_GRAVITY = 627.1f;

    /// One sinusoid of the sea, as the shader reads it.
    ///
    /// **One height field, differentiated twice.** The normal is its gradient and the caustics are
    /// its curvature, so the two cannot disagree about where a crest is — which they would the
    /// moment either sampled a field of its own.
    struct GpuWave
    {
        /// Unit vector the wave travels along.
        vec2 mDirection;

        /// Radians of phase per world unit.
        float mWavenumber;

        /// Half the crest-to-trough height, in world units.
        float mAmplitude;

        /// Radians of phase per second, from the dispersion relation at this depth.
        float mSpeed;
    };

    /// The circle constant, and the Lambertian BRDF's reciprocal of it.
    ///
    /// Shared because the shader divides every light by `INV_PI` and a lamp's intensity is built
    /// with the matching factor so that the two cancel — a relationship that only holds while both
    /// sides read the same number.
    const float PI = 3.14159265f;
    const float INV_PI = 1.0f / PI;

    /// What an isotropic phase function is worth: one over the solid angle of the whole sphere.
    ///
    /// **A light owes this to the air even with no phase function of its own.** A lamp reaches a
    /// point in the fog as *irradiance*, the same as it reaches a surface, and what comes back
    /// toward the eye is that irradiance spread over every direction — so the air scatters `1/4pi`
    /// of it this way. Left out, lamps light the air twelve and a half times too strongly, which is
    /// a lantern with a white sphere around it rather than a halo.
    const float INV_FOUR_PI = 0.25f * INV_PI;

    /// Irradiance of the sun against the sky it is set in.
    ///
    /// Not a physical figure: exposure absorbs any overall scale, so what matters is the ratio
    /// between the direct sun and the sky, roughly five to one on a clear day on a surface facing
    /// it. Shared with the shader because everything else on this scale is measured against it.
    const float DAYLIGHT = 8.0f;

    /// Angular radius of the sun, in radians — a disc about half a degree across.
    ///
    /// The real figure, because there is only one right answer and nothing about this renderer wants
    /// a different sun. It decides how wide the disc in the sky is drawn, and with it how wide the
    /// glitter path on water is: the two are the same number seen twice, one directly and one in a
    /// mirror, and they cannot be allowed to disagree.
    const float SUN_ANGULAR_RADIUS = 0.004654f;

    /// What an emissive of one is worth, as light.
    ///
    /// **The original's scale is not this renderer's.** There a fully lit surface reached one and an
    /// emissive of one matched it; here the direct sun is `DAYLIGHT`, so the same number has to be
    /// carried across or a glow that read as bright becomes a rounding error. Matched to the sky
    /// rather than to the sun, which is about a fifth of it: what these materials are for is being
    /// visible in shade, and a glowing mushroom is not as bright as the sun on it.
    const float EMISSIVE_INTENSITY = DAYLIGHT * 0.2f;

    /// How far a ray carries fog before whatever is behind it stops mattering.
    ///
    /// Four hundred metres. Past this the transmittance of even the thinnest weather is a rounding
    /// error, and a ray that hit nothing has to stop somewhere.
    const float FOG_REACH = 30000.0f;

    /// The height over the fog's base at which its density falls to `1/e`, in world units.
    ///
    /// Seventy units to the metre, so about thirty-seven of them — a layer deep enough to fill a
    /// valley and still thin out over the hill beside it.
    const float FOG_HEIGHT = 2600.0f;

    /// What shading a hit takes. `Rtx::MaterialKind`, which these must agree with.
    const uint KIND_SURFACE = 0u;
    const uint KIND_TERRAIN = 1u;
    const uint KIND_WATER = 2u;

    /// Water's index of refraction, and the reflectance it gives head-on.
    ///
    /// `((1.333 - 1) / (1.333 + 1))^2`, which is why water is a window seen from above and a mirror
    /// seen along it.
    const float WATER_IOR = 1.333f;
    const float WATER_F0 = 0.02f;

    /// Extinction per world unit, per channel — how fast water swallows light along a path.
    ///
    /// **Red goes first and the rest goes slowly**, which is the one thing about water's colour that
    /// is not a matter of taste and is why shallow water reads green where deep water reads blue.
    /// Jerlov's coastal type, quoted per metre as 0.32, 0.05 and 0.08, over the game's 69.99 units
    /// to the metre. Every expectation a test makes about water derives from this, so a tuning pass
    /// is one line rather than five pieces of arithmetic that quietly stop describing the shader.
    const vec3 WATER_EXTINCTION = vec3(0.004572f, 0.000714f, 0.001143f);

    /// The single-scattering albedo: the share of extinction that was scattering and not absorption,
    /// and so the part the water hands back as its own colour instead of swallowing.
    ///
    /// **This is what decides whether deep water is dark.** A channel whose scattering albedo
    /// approaches one settles at a bright colour however deep it gets — a milky sheet. Clear
    /// tropical water really does behave that way, because molecular scattering dominates its blue;
    /// a tannin-stained coastal swamp does not, and this game's water is the second.
    const vec3 WATER_SCATTER = vec3(0.012f, 0.042f, 0.040f);

    /// Which instances a ray is interested in.
    ///
    /// **Water must not cast a shadow, and the mask is how traversal is told so at no cost.** The
    /// alternative — building water non-opaque so the candidate loop can wave shadow rays past — was
    /// measured at half the frame rate, because every shadow ray crossing the sea then invokes a
    /// shader where traversal alone had been enough.
    const uint MASK_SOLID = 0x01u;
    const uint MASK_WATER = 0x02u;

    /// Where a mesh's vertices and indices begin in the shared buffers.
    ///
    /// Indices are mesh-local, so a triangle's vertex is `mVertexOffset` plus what the index says.
    struct GpuMesh
    {
        uint mVertexOffset;
        uint mIndexOffset;
    };

    struct GpuInstance
    {
        uint mMesh;
        uint mMaterial;
    };

    /// One point light, with everything a shader needs already derived.
    ///
    /// The colour is folded into the intensity and the reach is not the radius the record carried;
    /// both are settled on the way in, so the shader has one falloff to evaluate and no rules to
    /// remember. `Rtx::Light` says why each is what it is.
    struct GpuLight
    {
        vec3 mPosition;
        vec3 mIntensity;
        float mReach;
    };

    /// One layer of terrain: a tiling ground texture and the weights that place it.
    ///
    /// A chunk is four or five of these summed. The mask is a grid of weights in the shared mask
    /// buffer rather than a texture, because it is ten texels across — a whole cell's worth fits in
    /// tens of kilobytes, and sampling it by hand is what lets the edges clamp instead of inheriting
    /// the repeat every other texture in the game needs.
    struct GpuLayer
    {
        uint mDiffuse;
        uint mMaskOffset;
        uint mMaskWidth;
        uint mMaskHeight;

        /// Chunk texture coordinates to this layer's, as `uv * xy + zw`.
        vec4 mDiffuseTransform;
        vec4 mMaskTransform;
    };

    struct GpuMaterial
    {
        /// One of the `KIND_` values.
        uint mKind;

        uint mDiffuse;

        /// The alpha below which a texel is a hole, or zero where the surface has none.
        ///
        /// The mode it came from does not survive the trip: what a cutout costs traversal is one
        /// comparison, and a material that wants none stores a threshold nothing can fail. Which
        /// instances stop to make that comparison at all is settled by the build, from the same
        /// number.
        float mAlphaCutoff;

        /// Where this material's terrain layers are, or a count of zero for a single-textured
        /// surface — which is everything but the ground.
        uint mLayerOffset;
        uint mLayerCount;

        /// A map of what glows and how much, or `NO_TEXTURE`. Added past the albedo rather than
        /// through it, which is where the original engine adds it.
        uint mEmissive;

        vec4 mDiffuseColour;

        /// How much the surface glows regardless of what falls on it, with the material's own
        /// multiplier already folded in.
        ///
        /// **A lighting term, not a colour beside one.** The original engine sums it with the
        /// diffuse and ambient light and multiplies the whole by the texture, so a mushroom cap
        /// carrying half against its stalk's nothing glows *with its texture in it*. Added past the
        /// albedo instead, the cap comes out flat white.
        vec3 mEmissiveColour;
    };

#ifdef __cplusplus
}
#endif

#endif
