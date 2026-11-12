#ifndef OPENMW_COMPONENTS_RTX_SHADERS_LOOK_H
#define OPENMW_COMPONENTS_RTX_SHADERS_LOOK_H

#include "hosttypes.h"
#include "portable.h"
#include "scene.h"

// Every number that decides how the picture looks, in one file, so that tuning it is reading one
// file rather than remembering which of thirty holds the dial.
//
// **What belongs here is a number somebody turns to change the frame**: a taste dial nobody
// measured, a real constant that fixes a colour, or a sample count that fixes how clean the frame
// is. What does not belong is everything the picture does not depend on — units and maths, buffer
// and grid sizes, workgroups, traversal masks, enumerations, and the biases and guards that keep
// the arithmetic honest — which stay beside the code that has to agree with them.
//
// **One dial stays behind, and says why where it stands.** `WATER_CAUSTIC_MAX` in `scene.h` clips
// the brightest filament, and the polynomial beneath it is fitted against that clip: the two are
// one statement.
//
// The order is the order the light travels in reverse, from the eye outward: exposure and grade,
// the sky, the surfaces, the bounce, the air, the water, the sprites, and last the frames and
// samples that settle the result.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// How many times brighter the day is than the scale the lamps, the rooms and the night are set
    /// on: what the sun, the sky and everything the sky sends are multiplied by with the sun fully up.
    ///
    /// **Morrowind set its lamps, its rooms and its nights on one scale with its day.** In the world
    /// a day outshines a lantern's light on the wall beside it some twenty thousand times. Here the
    /// day is lifted by this and the exposure adapts to the lift in full, so a sunlit frame is shown
    /// as bright as before, and what the day does not lift — lamps, flames, glows — falls under it by
    /// this much. Set by eye and well under the world's figure: at ten, a lamp at half its radius
    /// still gives a shaded wall a fifth again.
    ///
    /// **Geometric in the sun's share**, so each step of a dawn adds the same number of stops, which
    /// is twilight's own shape. One at night, in a room and in a frame built by hand.
    ///
    /// **First in the file, because the exposure's ranges are sized by it**: `MAX_LOG_LUMINANCE`,
    /// `EXPOSURE_MIN` and, through that, `MAX_SUN_RADIANCE`. Past about sixty-five the sun's capped
    /// disc no longer fits the half floats a shown frame is stored in.
    const float DAYLIGHT_GAIN = 10.0f;

    /// Darkest luminance the histogram resolves, as a power of two: about an eight-thousandth of
    /// white, which is below anything a lit surface reaches and under the darkest corner of an
    /// unlit interior that is still a picture.
    const float MIN_LOG_LUMINANCE = -13.0f;

    /// Brightest, as a power of two. Sixty-four times white covers a flame seen directly, and the
    /// day's gain lifts every sunlit pixel past that by `DAYLIGHT_GAIN`: `6 + log2(DAYLIGHT_GAIN)`,
    /// written as a literal for the reason `portable.h` gives.
    const float MAX_LOG_LUMINANCE = 9.32192802f;

    /// Where a pixel stops being binned and starts being counted as black: the bottom of the scale,
    /// `2^MIN_LOG_LUMINANCE`, written as the fraction it is exactly. **One edge and one number**:
    /// a black threshold above the bottom of the scale sends the band between them to the lowest
    /// bin, metered at that bin's middle and so brighter than it is. Without it the dark areas of
    /// an interior pile into the lowest bin and drag the average down, and the exposure opens until
    /// the few lit surfaces are white.
    const float EXPOSURE_BLACK = 1.0f / 8192.0f;

    /// The luminance a correctly exposed mid grey sits at. Eighteen per cent is the photographic
    /// convention, and it is what puts an average scene in the middle of the curve rather than at
    /// an end.
    const float EXPOSURE_KEY = 0.18f;

    /// How completely the eye adapts to what it is looking at.
    ///
    /// **One would be a renderer with no night in it.** Dividing by the mean normalises every frame
    /// to the same brightness, so midnight, an interior and noon all come out within a couple of per
    /// cent of each other. The response is compressive: a room at dusk goes on looking dimmer than
    /// the same room at noon however long you sit in it.
    ///
    /// Rendered luminance is `KEY^a * mean^(1-a)`, so at three quarters a scene fifty times darker
    /// comes out two and a half times darker rather than identical, and a scene already at the key
    /// is left alone. Inherited from the reference implementation, which measured it.
    const float EXPOSURE_ADAPTATION = 0.75f;

    /// How far the metered exposure is moved, in stops: one doubles the picture and minus one
    /// halves it.
    ///
    /// **The dial for how bright the picture is, and not `EXPOSURE_KEY`.** The key is the convention
    /// the meter aims at and it passes through `EXPOSURE_ADAPTATION`'s power, so doubling it moves
    /// the picture by `2^0.75` and by nothing in a scene already at the key. This multiplies what the
    /// meter settled, as a camera's exposure compensation does, so a stop is a stop in every scene.
    ///
    /// Taken with the hour's bias, before `EXPOSURE_MIN`, `EXPOSURE_MAX` and the eye's approach, so a
    /// frame moves toward the compensated value rather than jumping to it. An exposure a run holds
    /// fixed — a reference, a test — is a number the run chose, and this does not move it.
    const float EXPOSURE_COMPENSATION = 0.0f;

    /// How far a lit scene is moved from what the meter settled, in stops: the whole of it in a room
    /// and under a full sun, fading with the sun's share to nothing at night.
    ///
    /// **The dial for a day or a room that reads too bright, where `EXPOSURE_COMPENSATION` would
    /// take the night down with them.** A night's share is nought, so its exposure does not move by
    /// a bit (`makeSkylight`, `makeRoomLight`). Taken before `EXPOSURE_MAX`, so a dim room the
    /// ceiling already holds stays as it is, and only a room that meters bright comes down.
    const float EXPOSURE_LIT_STOPS = -0.5f;

    /// Clamped rather than trusted: a frame that is almost entirely black would otherwise divide by
    /// something near zero and hand back an exposure that turns the next frame's noise into a
    /// snowstorm. `MAX_SUN_RADIANCE` is sized against the floor, and the floor against the day: a
    /// day lifted by `DAYLIGHT_GAIN` meters that much lower.
    const float EXPOSURE_MIN = 0.6f / DAYLIGHT_GAIN;

    /// **And how far the eye opens at most, which is the look of a dark room**, as Unreal's
    /// `MaxEV100` is. A room lit by its lamps alone meters thousands of times under the key, and
    /// `EXPOSURE_ADAPTATION` alone opened every interior measured to between 58 and 100, so a
    /// candlelit room stood brighter than the night outside it. Held here, an interior shows its
    /// lamps' own light at one exposure, and a dim room is dimmer than a bright one by all of the
    /// difference. What it costs: the eye adapts no further inside, so a dark corridor off a lit
    /// room stays dark.
    const float EXPOSURE_MAX = 20.8f;

    /// Where the eye starts once it has no past: a bright day's exposure, which the eye then
    /// opens from toward what the frame measures.
    ///
    /// **Dark to normal, and never bright to normal.** A load or a cut resets the eye on the first
    /// frame of a world that arrives over the frames after it, and taken outright, that frame's
    /// measurement exposed for a world half there: the picture stood bright and closed over
    /// `EXPOSURE_FALL_SECONDS`. Started at a day, an exterior in daylight starts where it settles
    /// and an interior opens from the dark, as an eye does coming in from outside.
    ///
    /// **A day is the key lifted by the day's gain**, so the meter settles at
    /// `(1 / DAYLIGHT_GAIN)^EXPOSURE_ADAPTATION * 2^EXPOSURE_LIT_STOPS`, `10^-0.75 * 2^-0.5`, written
    /// as the literal it is for the reason `portable.h` gives.
    const float EXPOSURE_DAY = 0.12574334f;

    /// How long the exposure takes to open, as the time constant of an exponential approach, in
    /// seconds.
    ///
    /// **The eye is not symmetric, so neither is this.** Adapting to darkness is the slow half and
    /// adapting to light the fast one. Held to a second and a half rather than the real figure,
    /// because a cave that stayed black for a minute is a renderer nobody can play; stepping out of
    /// a door still dazzles and stepping into one still takes a moment to resolve.
    ///
    /// Set by eye. What is not by eye is the shape: a gap closed at `1 - exp(-dt / tau)` closes by
    /// the same fraction per second whatever the frame rate.
    const float EXPOSURE_RISE_SECONDS = 1.5f;

    /// And how long it takes to close, which is the eye meeting light rather than losing it.
    const float EXPOSURE_FALL_SECONDS = 0.5f;

    /// The share of the lit pixels, from the darkest up, the meter leaves out below the mean, and
    /// the share up to which it reads: the darkest tenth and the brightest tenth are left out.
    /// **Unreal's defaults since 4.25** (`FCameraExposureSettings::LowPercent` and `HighPercent`): a
    /// handful of flames in a dark room no longer pulls the eye shut, and a corner of shadow no
    /// longer opens it. Read as the mean of the log luminance over what is left.
    const float EXPOSURE_LOW_SHARE = 0.1f;
    const float EXPOSURE_HIGH_SHARE = 0.9f;

    /// How much the curve takes off the darkest channel once it has any to take. Khronos's own.
    const float TONE_SHADOW_OFFSET = 0.04f;

    /// Where `toneMap` stops leaving a colour alone and starts bringing it down.
    ///
    /// Khronos's own, less the shadow offset, which it has already taken off by then.
    const float TONE_COMPRESSION_START = 0.8f - TONE_SHADOW_OFFSET;

    /// How far a compressed colour is carried toward white. Khronos's own.
    const float TONE_DESATURATION = 0.15f;

    /// How much colour the picture keeps: one as traced, nought grey, and past one more than traced.
    ///
    /// **Each colour is carried toward its own luminance, in linear light, before the curve**, which
    /// is where a grading pass puts its saturation. The luminance is `LUMINANCE_WEIGHTS`', so the
    /// grade moves no brightness the eye weighs, and highlights still go to white by the curve's
    /// own roll-off.
    const float TONE_SATURATION = 0.85f;

    /// How far from mid grey the picture is spread, as stops per stop: one as traced, under one
    /// flatter, over one harder.
    ///
    /// **A luminance `n` stops from `EXPOSURE_KEY` is moved to `n * TONE_CONTRAST` stops from it**,
    /// a grading pass's contrast: a power about mid grey in log space. The pivot is the key because
    /// that is the grey the exposure just put there, so the dial leaves the level of the picture
    /// alone. The whole colour is scaled by what its luminance moved, so no hue or saturation
    /// changes. Before `TONE_SATURATION` and the curve, like it.
    const float TONE_CONTRAST = 0.95f;

    /// How much of each coarser level survives into the one above it.
    ///
    /// **The pyramid is mixed rather than summed**, which is what keeps the total independent of
    /// how many levels there are: `mix(finer, coarser, this)` at every step, so a frame that built
    /// one level fewer is a narrower bloom and not a dimmer one. Higher is a wider, softer veil.
    const float BLOOM_SCATTER = 0.6f;

    /// How much of the pyramid is left in the picture.
    ///
    /// **No threshold anywhere, which is why this is small.** A lens spreads every photon that
    /// reaches it, so the whole frame is blurred and mixed back at a few per cent, where a threshold
    /// makes bloom arrive as an object crosses a brightness nobody can see. What makes a Morrowind
    /// sun read as a sun is that its disc is a hundred times the median of the frame around it.
    const float BLOOM_STRENGTH = 0.04f;

    /// Irradiance of the sun against the sky it is set in.
    ///
    /// Not a physical figure: exposure absorbs any overall scale, so what matters is the ratio
    /// between the direct sun and the sky, roughly five to one on a clear day on a surface facing
    /// it. Shared with the shader because everything else on this scale is measured against it.
    const float DAYLIGHT = 8.0f;

    /// Angular radius of the sun, in radians — a disc about half a degree across.
    ///
    /// The real figure, because nothing about this renderer wants a different sun. It decides how
    /// wide the disc in the sky is drawn and how wide the glitter path on water is: the same number
    /// seen twice, directly and in a mirror, which cannot be allowed to disagree.
    ///
    /// **The sun the content paints is not read, and bloom stands in for it.** The rasterizer draws
    /// `tx_sun_05` on a quad three and a half degrees across and, over it, the additive flash
    /// `tx_sun_flash_grey_05` 2.6 times wider, scaled by the seen share its occlusion query eases.
    /// The trace draws this disc, uniform, and its bloom spreads it into the halo both sprites
    /// paint, eased by the same seen share through the glare (`SunGlarePass`).
    const float SUN_ANGULAR_RADIUS = 0.004654f;

    /// Angular radius of the cone a sun shadow ray is drawn from, in radians: three degrees.
    ///
    /// **Wider than the disc, on purpose, and the disc is not moved with it.** A shadow cast by the
    /// real half degree has a penumbra a centimetre wide on a wall two metres behind what casts it,
    /// a hard edge on a screen, and Morrowind never had one: its shadow maps are filtered and soft
    /// at every distance. Two degrees still cut a hard edge across a wall a short way behind its
    /// caster — the round stone wall at Seyda Neen's (-2, -9), where a converged reference drew the
    /// same edge, so the light and not a filter. Three degrees puts thirty-one units of penumbra
    /// there, and Unreal's and Unity's suns are widened past their half degree for the same reason.
    ///
    /// A choice about the look and not a measurement, so a constant of its own: the disc in the sky
    /// and the glitter path on the water stay at the real size, since a sun seen wider is a
    /// different sun. A sun seen through haze does widen its own shadows, and if this ever wants to
    /// follow the weather, that is the model to follow it with.
    const float SUN_SHADOW_RADIUS = 3.0f * PI / 180.0f;

    /// How fast the cone a sky light's shadow ray is drawn across widens below the water, in radians a unit of
    /// the path the light crossed under the surface — ten degrees a hundred units — and the widest it gets.
    ///
    /// **A choice about the look, as `SUN_SHADOW_RADIUS` is, with a physical shape**: the waves bend the
    /// refracted light a few degrees either way and the water scatters it a little forward, so light deep in
    /// the water arrives across more of the sky than over it, and a shadow under the surface is the softer the
    /// deeper it falls. Without it the leg under the water was drawn along one line, and whatever stood in the
    /// water cast a razor edge on the bed.
    const float WATER_SHADOW_SPREAD = 10.0f * PI / 180.0f / 100.0f;
    const float WATER_SHADOW_RADIUS = 30.0f * PI / 180.0f;

    /// What a moon's own texels are worth as radiance.
    ///
    /// **Pinned, and not by taste.** A real full moon is a 640,000th of the sun and there is no scale
    /// this renderer could put both on, so the number has to be chosen: a moon bright enough to blow
    /// all three channels is a white disc whatever colour it was given. `tx_masser_full`'s mean
    /// opaque texel is 0.0332 in red, and this takes that to 0.18, the most red a moon can be and
    /// still be a moon.
    ///
    /// It multiplies the portrait where one is loaded and the portrait's mean where none is, so the
    /// two paths are the same brightness and only the detail differs.
    const float MOON_RADIANCE = 5.4217f;

    /// How much of the sunlight falling on Masser comes back off it: its geometric albedo, which is
    /// what a body sends back at opposition against what a perfectly diffusing disc of the same size
    /// would.
    ///
    /// **The Moon's own 0.12, because Morrowind's moons are rock, raised by eye**, so that a clear
    /// night's ground reads as moonlit. A disc of geometric albedo `p` and half-angle `t` under
    /// irradiance `E` delivers `E * p * sin(t)^2` to a surface facing it, and every term of that is
    /// already here: `DAYLIGHT` is the sun, and `Moons_<name>_Size` is the angle.
    ///
    /// **The size of Morrowind's moons is what makes the physics usable.** Earth's moon is half a
    /// degree across, which at 0.12 puts real moonlight at a 407,000th of sunlight, a figure no
    /// frame here could carry beside a noon. Masser is twelve degrees across at OpenMW's own
    /// `Moons_Masser_Size` and twenty-one at the ini's, which puts it between a 291st of the sun and
    /// a 102nd.
    ///
    /// **Masser's alone, because the two moons do not reflect the same.** `tintOf` normalises both
    /// portraits on Masser's luminance, so Secunda carries this times the 2.54 its own paler face
    /// says it is worth. And this is not `MOON_RADIANCE`, which is pinned by where the tone curve
    /// stops keeping colour: a light read out of it would put a night at a thousandth of a day.
    const float MOON_ALBEDO = 0.30f;

    /// What a texel of the star field is worth as radiance.
    ///
    /// **Morrowind puts a star at the top of the display range.** `paintAtmosphereNight` hands the
    /// sheet's own texel to a `(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` blend, so a white texel at full
    /// alpha lands on the frame buffer at one, and every pixel of the blob a star is spread over
    /// saturates into a hard dot. Nothing here clips, so the level has to put a star where the
    /// content puts it.
    ///
    /// **A star is still never brighter than a full moon**, read against Masser's brightest texel and
    /// not its mean: a portrait's peak is several times its mean.
    ///
    /// **It reaches what is drawn and never what lights.** A bounce that escapes takes `skyGlow`,
    /// which carries the sheets as one mean — `NightSky::mGlow` — so raising this raises that too
    /// and `Skylight::mFill` takes it back out of the weather's own ambient.
    ///
    /// **The level a star was matched to the content at**, 0.45 of a texel through DLSS's fifth off
    /// the brightest star; FSR at `native` left a clear night's star peaks where no upscaler did
    /// (a median of 119 against 113 of 255 at Balmora at one in the morning).
    const float STAR_RADIANCE = 0.36f;

    /// What a texel of the three nebulae is worth as radiance.
    ///
    /// **A wash over the sky it washes.** Most of a Morrowind night's colour is in these rather than
    /// in the stars: two of the three reach past a radian, so they tint half the sky at a time.
    ///
    /// **Against the sheet's mean of `rgb * a`, which is what `skyPatches` draws**, and not its colour
    /// with the alpha ignored: `tx_stars_nebula` averages 0.002 that way, so this puts a nebula's
    /// average at a twentieth of what `Sky_Night_Color` decodes to.
    const float NEBULA_RADIANCE = 0.06f;

    /// What the engine paints the second row of its cloud mesh with.
    ///
    /// `ModVertexAlphaVisitor::Clouds`'s own 64 over 255, and the only number in the deck's fade
    /// that is not a radius read off the mesh. `CloudShell::mRings` carries where it applies.
    const float CLOUD_RING_ALPHA = 64.0f / 255.0f;

    /// How far above its own mean a texel of a cloud sheet reads as a cloud in full sunlight.
    ///
    /// **What the sheet's paint is a ratio against.** `CloudDeck::mMean` divides a texel's luminance
    /// by what its sheet averages, and this is where that ratio reaches one: a texel at the mean is
    /// half way between a cloud's shadow and its lit face, and one at twice the mean is lit through.
    ///
    /// **Two rather than one, because one is where the shape goes.** Half a sheet's texels lie above
    /// its mean by definition, so a scale that saturated there would flatten them onto one value.
    /// Over the six sheets the shipped fallbacks reach, the 99th percentile of the ratio runs 1.10
    /// to 1.63 and the brightest texel is 2.03.
    const float CLOUD_THICKNESS_MAX = 2.0f;

    /// How much of the light landing on a cloud deck leaves the underside of it.
    ///
    /// **A cloud is darker than the sky it covers, and this is the whole of why.** Water droplets
    /// scatter nearly everything that reaches them, but most of it leaves *upward*: plane-parallel
    /// theory puts a deck's transmission at 0.2 to 0.3. At 0.9, the figure a "lit from the whole
    /// hemisphere" argument suggests, a night deck is 90% of the sky it hides and cannot be seen.
    ///
    /// Thin cloud is not dragged down with it: how much sky a wisp replaces is its own alpha, which
    /// is `cloudDeck`'s coverage and not this.
    const float CLOUD_TRANSMISSION = 0.25f;

    /// What a surface with no diffuse map reads: white, the rasterizer's `vec4(1.0)`, so its colour
    /// is its material's. A map that did not load reads the backend's grey stand-in instead.
    const vec3 UNTEXTURED_ALBEDO = vec3(1.0f, 1.0f, 1.0f);

    /// The radiance a fully lit white card leaves, which is what the original's one meant.
    ///
    /// **What a texel of one is worth wherever the original added the content rather than lighting
    /// it.** There a fully lit surface reached one, so a flame sprite at one and a sphere-mapped
    /// sheet at one both reached that same white; here a Lambertian card under `DAYLIGHT` leaves
    /// `DAYLIGHT / pi`, and carrying their one onto this renderer's scale is multiplying by it. One
    /// constant, because one derivation: a flame and an enchanted item's caustic sheet are the same
    /// convention met on two kinds of geometry.
    ///
    /// **This is not `EMISSIVE_INTENSITY` below**, which is a material's own glow, measured against
    /// rendered frames rather than derived, and about three times this. Content carried at that scale
    /// puts every texel past white at night, and a flame or a sheet reads as a cut-out that switches on.
    const float SUNLIT_WHITE = DAYLIGHT * INV_PI;

    /// What an emissive of one is worth on screen.
    ///
    /// **The original's scale is not this renderer's.** There a fully lit surface reached one and an
    /// emissive of one matched it; here the direct sun is `DAYLIGHT`, so the same number has to be
    /// carried across or a glow that read as bright becomes a rounding error.
    ///
    /// **What sets it is what a night frame shows.** A night's exposure is metered off a dark scene,
    /// so a surface held high enough washes to white and loses the pattern on it. About three times
    /// `SUNLIT_WHITE`, chosen against rendered frames rather than derived, because a material's own
    /// glow and painted content added past the lighting are two conventions and not one.
    ///
    /// **A glow has no lamp of its own, and lights only by the bounce that lands on it**
    /// (`landingLightOf`): a lamp for every glowing shape is hundreds of lights in the grid per
    /// cell, and what they buy is the warm ring under a mushroom's cap. **A lamp's own model does not
    /// light even so**: its `LIGH` lamp already lights for it (`INSTANCE_LAMP_BODY`).
    const float EMISSIVE_INTENSITY = 8.0f;

    /// What light on the far side of a leaf is worth to the side being looked at, against the same
    /// light on the near side.
    ///
    /// **A leaf is a sheet with a mask, and it is the one surface in the game lit from behind.** A
    /// real leaf passes about half of what it reflects, so a canopy against the sun glows through
    /// rather than going black. The same albedo on either side: what colours the light through a
    /// leaf is the leaf. Half, rather than the tenth a leaf transmits absolutely, because the term
    /// scales the surface's own diffuse response and not the sun.
    ///
    /// **The far side's light is the term, and the shadow it would have cast stays whole.** What a
    /// leaf lets through to the ground under it arrives by the bounce that lands on the leaf's
    /// underside, so a shadow ray thinning itself through the leaf's body as well would deliver the
    /// same light twice.
    const float SHEET_TRANSMISSION = 0.5f;

    /// The reflectance at normal incidence of whatever a `_spec` map calls no metal: glTF 2.0's 4%,
    /// an index of refraction of 1.5. A vanilla surface is none of these — it has no specular map and
    /// reflects nothing, which is what its NIF says (`nifloader.cpp` sets specular to black on every
    /// Morrowind mesh).
    const float DIELECTRIC_F0 = 0.04f;

    /// How far a normal map's height shifts the texture coordinates toward the eye, as the
    /// rasterizer's `parallax.glsl` has it: `eye.xy * (height * SCALE + BIAS)`, the eye in the
    /// tangent frame. OpenMW's numbers and not chosen, so a surface is the one the content was
    /// authored against.
    const float PARALLAX_SCALE = 0.04f;
    const float PARALLAX_BIAS = -0.02f;

    /// The most steps a march through a relief takes (`reliefToward`): where its crossing is wider,
    /// the march reads a coarser level, so each step stays a texel of the level it reads. RTX Remix
    /// caps its march at 64 and Tatarchuk's runs 8 to 50 (`docs/research/2026-11-08-parallax-
    /// occlusion-techniques.md`).
    const float RELIEF_STEPS = 32.0f;

    /// What the reflectance at grazing is, as a multiple of the reflectance at normal incidence's
    /// green, saturated at one: `F90 = saturate(50 * F0.g)`.
    ///
    /// **No real material reflects under 2% at normal incidence**, so less is taken as specular
    /// occlusion the content painted and the edge darkens with it (Filament, Unreal's `F_Schlick`).
    /// It also makes a reflectance of nought reflect exactly nothing at every angle, which keeps a
    /// vanilla surface the Lambert surface it was.
    const float SPECULAR_EDGE_SCALE = 50.0f;

    /// The smoothest perceptual roughness a light is evaluated at.
    ///
    /// **The sun and the lamps are small, and a lobe narrower than they are is a spike the one ray
    /// drawn across them lands in or misses.** Filament's floor for 32-bit analytic lights; it holds
    /// the lobe's peak finite and is below any roughness the installed maps paint (steel reads 0.36).
    const float ROUGHNESS_FLOOR = 0.045f;

    /// What share of indirect hits out of doors are lit at all, the rest paying by weight.
    ///
    /// **The two rays a bounce hit spends on direct light are the dimmest pair in the frame.** A sun ray
    /// and a lamp ray are traced there to light a term nothing resolves on its own; the moons are
    /// already refused that path for this reason, and the ambient ray beside them is drawn at
    /// `AMBIENT_EXTERIOR_RATE`.
    ///
    /// **Drawn and divided, so the estimate is unbiased by construction.** What it hands the filter
    /// is variance in the indirect channel, which is why it is judged on a moving camera.
    ///
    /// **Half the rays are not half the time.** The rays here are short: a lane that skips one does
    /// not release the warp, which runs on until the lanes that kept theirs are done, and a hashed
    /// draw leaves no warp with thirty-two skipping lanes. The ambient ray runs to `mReach` and is
    /// nearly all empty traversal, so halving those halves what the device does. **A rate is worth
    /// what the ray it drops is long.**
    ///
    /// **Out of doors only.** A room's indirect light *is* its lamps seen once off a wall, so rating
    /// it there halves the samples of the term that carries the room. Every interior view renders
    /// bit-identically under this, which `shot --against` says.
    ///
    /// **Judged under the accumulator, the wavelet and FSR**, still and strafing in, under both
    /// profiles: a quarter, a half and all of it stand within 0.02 of each other at the pier and in
    /// the guild. What a lower rate would save is a short ray a warp waits out anyway.
    const float INDIRECT_LIGHT_RATE = 0.5f;

    /// How fast a bounce ray's cone widens, against a primary ray's.
    ///
    /// A diffuse bounce spreads over the whole hemisphere, and what the indirect term wants from a
    /// texture is its *average*, so the cone is opened to about a radian, which reads the coarse mips
    /// a bounce should see without collapsing every one to the top level.
    const float BOUNCE_SPREAD = 1.0f;

    /// How far out of doors a surface traces its own bounce, in world units.
    ///
    /// **Beyond it the hemisphere is not traced and the escape is taken as though nothing stood in the
    /// way.** The far half of an exterior is thousands of pixels whose bounce ray crosses the whole
    /// acceleration structure and mostly finds sky anyway, and whose indirect term the filter then
    /// averages flat. One cell, the distance Morrowind itself builds a world in: nearer than that a
    /// bounce fills a doorway, an arch and the shaded side of a house.
    ///
    /// **Biased, unlike the rates beside it, and it is the bias that makes it worth having.** A draw
    /// and a divide would keep the traversal on half the pixels and the noise on all of them; this
    /// stops the traversal outright, and pays in ground lit slightly flatter. Exterior-only for the
    /// reason `AMBIENT_EXTERIOR_RATE` is: a room's escape is nothing at all, so a surface far down a
    /// hall would go dark rather than flat.
    ///
    /// **Ground alone, because distance does not say ground.** A draw about open hillside reaches the
    /// sky whatever stands nearby; the same draw about a wall spends half of itself on whatever the
    /// wall is attached to, and handing that the sky makes it too bright. A Vivec canton is one face
    /// hundreds of units tall running well past the reach, so an escape asked of it cuts through the
    /// building and the seam sweeps across it as the camera moves. Asked of ground alone, the view is
    /// byte-identical to one with every bounce traced.
    ///
    /// **What it is worth depends on where the camera stands**: nothing at eye level, where far ground
    /// is crowded under the horizon, and a lot for a camera looking at a cell from outside it.
    const float BOUNCE_REACH = 8192.0f;

    /// How far a room's fill looks for what is standing over a point, in world units.
    ///
    /// **Two metres, which is the furniture and not the room.** A cell's `AMBI` ambient is a flat stand
    /// in for every bounce the room makes, and reaching a point wedged under a pillow as fully as one
    /// in the middle of the floor is white cloth lighting its own contact shadow. What takes the fill
    /// away is what is close enough to be in front of the room rather than part of it, and anything
    /// further in Morrowind's small rooms is a wall.
    const float ROOM_FILL_REACH = 140.0f;

    /// What share of exterior points are asked whether they reach the sky, the rest paying by weight.
    ///
    /// **Out of doors the ambient ray is the expensive one, by two orders of reach.** It runs to
    /// `mReach` where a room's stops at `ROOM_FILL_REACH`, and it is nearly all sky: the traversal
    /// is spent proving that nothing is there.
    ///
    /// **A half and no further, because a third buys nothing.** A rate is a per-lane skip and the ray
    /// it skips is a long one, so a warp still runs until whichever of its thirty-two lanes kept a
    /// ray is finished, and at a third all thirty-two skipping is a chance in six hundred thousand.
    /// Judged under the accumulator, the wavelet and FSR, a quarter and all of it leave the pier's and
    /// the guild's noise where the half does, to the second decimal.
    ///
    /// **Drawn and divided by the draw, so the estimate is unbiased by construction.** What that
    /// hands the filter is variance, the same trade the moon pick makes. Nothing downstream clamps
    /// it: `pathEnd` and a sprite's fill both multiply, so a doubled sample stays worth double.
    ///
    /// **A filter takes every asker's answer**: the bounce's far hit's, a pane's (the pane filter's)
    /// and the fog's froxels'. **Hashed rather than blue noise, because two callers must not
    /// agree**: the bounce's far hit and a pane each ask this at one pixel with seeds of their own,
    /// and a screen-space tile has one arrangement per channel. The interior ray keeps every point:
    /// it is short, and a room is where this term does its visible work.
    const float AMBIENT_EXTERIOR_RATE = 0.5f;

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

    /// Where the fog pools when the cell has no water to gather over: sea level outdoors, and close
    /// enough to a floor to serve indoors.
    const float FOG_BASE = 0.0f;

    /// How large one cell of the coarsest scale is, in world units, and so how wide the whole tile is
    /// laid out at that scale.
    ///
    /// **Eighteen hundred, a bank the width of a house.** Neither way is bounded by aliasing, because
    /// fog keeps its shape where it is optically thick over a short distance; both are bounded by the
    /// look. A coarser grain, 1,400 to 3,000 units, gives fog whose shape is visible only from a
    /// ridge. A finer one reads as clots rather than weather: from a street at nine hundred, a bank
    /// is a puff a few metres across.
    ///
    /// **Sparse and dense rather than uniform and thin, with nothing tuned by hand.** `FOG_COVERAGE`
    /// divides the extinction back out, so a band that clears more of the ground thickens what is
    /// left by exactly as much, and neither number has to be re-tuned against the other.
    ///
    /// **Aliasing is what would stop this from shrinking, and the mip chain answers it**:
    /// `fogFieldAt` picks a level from the march's own stride, so the field is filtered rather than
    /// aliased.
    const float FOG_GRAIN = 1800.0f;
    const float FOG_TILE = FOG_GRAIN * float(FOG_FIELD_CELLS);

    /// The standard deviation of the sideways displacement the finer scales are read at, in world units.
    ///
    /// **Domain warping**: the *coordinate* is displaced by a noise of its own, so shapes stretch and
    /// curl instead of staying the round blobs a sum of octaves gives (Quilez's `fbm(p + w * fbm(p))`
    /// at one level). It costs nothing, because the coarse scale is fetched anyway and its second
    /// channel is a decorrelated field, so the pair is a vector already in hand. Horizontal only:
    /// the vertical shape of this fog is the height falloff.
    ///
    /// **Half a cell of the coarsest scale, because what a warp does is relative to what it bends.**
    /// A displacement much larger than the feature it moves is a second draw of the same field at an
    /// unrelated place, so a figure fixed in world units would start scrambling when the grain moved.
    const float FOG_WARP = FOG_GRAIN * 0.5f;

    /// The step between them. Not two, so the tiles never realign and repeat.
    const float FOG_LACUNARITY = 2.27f;

    /// The heading and speed each scale drifts on, coarsest first, in world units a second of the
    /// sky's clock.
    ///
    /// **The differing speeds are what stops it reading as a texture.** One field scrolling rigidly
    /// past is a pattern in motion; three shearing against each other make the shapes form and pull
    /// apart. The second and the third carry a little vertical drift, so banks rise and settle.
    /// Three names and not an array, because the host reads this file as well and reduces them
    /// against the clock (`Rtx::fogOffsets`).
    const vec3 FOG_CHURN_COARSE = vec3(11.0f, 7.0f, 0.0f);
    const vec3 FOG_CHURN_MIDDLE = vec3(-6.0f, 14.0f, 2.5f);
    const vec3 FOG_CHURN_FINE = vec3(19.0f, -4.0f, -1.5f);

    /// How far each finer scale's read is turned about the vertical, as the cosine and the sine of
    /// the turn; the coarsest is read unturned.
    ///
    /// **So that no two scales share a lattice.** A lattice noise has directions in it, where its
    /// features line up, and three scales of one volume read on one frame stack those directions
    /// rather than averaging them out. The angles are the two smallest Pythagorean triangles, so
    /// neither is near a quarter turn of the other: 3-4-5 is thirty-seven degrees and 5-12-13 is
    /// sixty-seven.
    const vec2 FOG_TURN_MIDDLE = vec2(0.8f, 0.6f);
    const vec2 FOG_TURN_FINE = vec2(5.0f, 12.0f) / 13.0f;

    /// The coarsest level of that chain a march is allowed to read.
    ///
    /// **Not the last one, because a level with too few texels left stops being the field.** At two
    /// texels a side the stretch a sampler reads runs the texels into the clamp: the mean drifts and
    /// the band clears more than it was measured against. Off the baked volume, the band leaves
    /// `FOG_COVERAGE` to within a twentieth down to four texels a side, then 0.396 at two and 0.173
    /// at one.
    ///
    /// **Four texels is the last level that holds, and that is level three of six.** Past it a step
    /// reads a field it cannot resolve and what comes back is noise, which the jittered step and the
    /// temporal filter take out.
    const float FOG_FIELD_COARSEST = 3.0f;

    /// What a recorded `Wind Speed` of one comes to in world units a second.
    ///
    /// **Read as a wind rather than picked, which is what it takes to make an ash storm look like one.**
    /// Twenty metres a second is a Beaufort 8 gale, and seventy units to the metre makes that 1,400.
    /// The ten then land where their names say: clear's 0.1 is a two-metre breeze, rain's 0.3 is six,
    /// thunderstorm's 0.5 is ten, ashstorm's 0.8 is sixteen, and blight and blizzard blow eighteen.
    ///
    /// The host integrates it over the sky's clock, which is real time at the shipped `timescale`
    /// rather than the game's thirty-times one, so this is a wind — and a time-lapse only where the
    /// clock is sped up. `Sky::skyStep` is the clock.
    const float FOG_GALE = 1400.0f;

    /// Below `FOG_CLEARING` of the field the air is clear, and at `FOG_SOLID` the fog is at full
    /// thickness. Between them it is a bank's edge.
    ///
    /// **This is what makes fog patchy rather than merely uneven**: cutting a band out of the noise
    /// gives banks with gaps between them, which is what a valley at dawn looks like.
    ///
    /// **The band has to be cut against the field's own spread, not picked.** Averaging octaves
    /// narrows a distribution sharply, and a threshold chosen for one octave's range clears almost
    /// everything. This field runs mean 0.5 with a standard deviation of `FOG_FIELD_SPREAD` by
    /// construction, at every level of the chain, so one pair of numbers stands for the band at every
    /// step of a march.
    ///
    /// **Sample it over a plane wider than the tile, not over a sphere**: a million pixels of a
    /// sphere of radius 5,000 is a million samples of about a tenth of one tile, and the mean it
    /// gives is wrong by several per cent while looking precise.
    const float FOG_CLEARING = 0.45f;
    const float FOG_SOLID = 0.65f;

    /// What that band comes to on average, which the coverage is divided by.
    ///
    /// **So the noise redistributes the air rather than removing it.** The extinction the host derived
    /// is what a ray should cross on average, Morrowind's own view distance turned into a
    /// coefficient, and a band that clears two thirds of the ground would make the world three times
    /// clearer than the game says. Normalised, the band's bank is `1 / FOG_COVERAGE`, 2.8 times the
    /// derived extinction, against a gap of nothing. `FOG_EXTERIOR_EVEN` says what the open air lays
    /// under it.
    ///
    /// **Measured, and it must be re-measured if the band or the field moves.**
    /// `theCoverageBandLeavesTheShareTheDensityIsDividedBy` computes it off the baked field to four
    /// figures, and `theBankedFieldHoldsAsMuchAirAsAnEvenOne` checks the frame agrees.
    const float FOG_COVERAGE = 0.3563f;

    /// How much of the open air is laid evenly, against the share the band banks.
    ///
    /// **Banked whole, a gap held no air at all and a bank 2.8 times the mean**, so every stretch of
    /// a street read as clear or as a wall. Real radiation fog is thinner between its banks and not
    /// gone. The mean is untouched, since both halves are normalised to it: a gap holds 0.4 of the
    /// air the weather records and a bank `0.4 + 0.6 / FOG_COVERAGE`, 2.1 times it. A room is even
    /// whole, `roomFog`.
    const float FOG_EXTERIOR_EVEN = 0.4f;

    /// The mean diameter of the fog's water droplets, in micrometres.
    ///
    /// **The one dial on the shape of the sun's halo.** Radiation fog runs from a few micrometres to
    /// about twenty, and the forward peak sharpens brutally with size: at five the fog scatters 1,300
    /// times an isotropic one straight down the sun's line, at eight 4,300, at thirty 81,000. Eight
    /// is a thick coastal fog.
    const float FOG_DROPLET = 8.0f;

    /// Below this share of what the sky puts into the air, the moons do not get their shadow ray.
    ///
    /// **What makes the cost fall only where the halos are.** Ninety degrees off a moon the phase
    /// function is two thousandths of its forward value, so the moon puts less light into the air
    /// there than the rounding on the sky's term.
    ///
    /// **The sun's ray is not gated by this**, although the same argument holds for the air: a puff
    /// of smoke in the same froxel reads the sun's shadow whichever way the eye looks, and
    /// `FogSources::mSunward` says so.
    const float FOG_SHAFT_FLOOR = 0.02f;

    /// How many cells of the light grid one ray may walk before it gives up.
    ///
    /// **A budget and not a limit anything reaches.** A froxel's ray is one slice long, and the grid
    /// starts at a cell of a quarter tile, so a ray crosses a dozen cells at most; this stops a fine
    /// grid under a long ray turning the walk back into the march it replaced.
    const uint FOG_CELLS_ALONG = 32u;

    /// What is left of a ray at the world's edge, once the second element of the air has had it.
    ///
    /// **The whole point of that element is that this is not a matter of taste.** The last ring of
    /// terrain ends in mid-air, and the only number that hides it is one small enough that the
    /// difference between the ground and the sky behind it is below what the frame can carry: one
    /// step of an eight-bit channel.
    const float FOG_EDGE_TRANSMITTANCE = 1.0f / 256.0f;

    /// Over what share of `VisibilityConstants::mFogEdge` that air closes, as the `1/e` length of
    /// its density.
    ///
    /// **Exponential in the range from the eye, which is what keeps it off the ground the player is
    /// standing on.** A density that grows by `e` every eighth of the reach leaves 0.905 of a ray at
    /// half of it and 0.473 at three quarters, so the world closes over its last quarter and the
    /// quarter before it is only softened.
    const float FOG_EDGE_RAMP = 0.125f;

    /// The sine of the climb above which that air is not there at all.
    ///
    /// **A ring on the ground and not a dome, because that is what is missing.** A ray that climbs
    /// leaves the terrain behind and finds sky, which needs no hiding, and air that closed over it
    /// too would put the horizon's colour across the whole upper sky. Twenty-five degrees covers
    /// everything within `tan(25)` of the reach above the eye, which at four cells is fifteen
    /// thousand units of mountain.
    ///
    /// **A climb alone, and a descent is never masked.** An eye high enough looks down on the ring
    /// where the loaded cells stop, and the steeper the view the more of the cut it can see.
    ///
    /// `sin(25°)`, written as a literal for the reason `portable.h` gives.
    const float FOG_EDGE_RISE = 0.42261827f;

    /// Which way the sea's waves run, in the world's plane: the rasterizer's water shader's own
    /// `WIND_DIR`, a constant there, and so not the weather's wind, which an ash storm turns as the
    /// player walks. Restated, because a header the GLSL reads cannot include the game's shader.
    const vec2 SEA_WIND = vec2(0.5f, -0.8f);

    /// `SEA_WIND` as the unit heading `VisibilityConstants::mSeaHeading` takes.
    RTX_SHADER vec2 seaHeading()
    {
        return normalize(SEA_WIND);
    }

    /// Water's index of refraction, and the reflectance it gives head-on: `((n - 1) / (n + 1))^2`,
    /// 0.02037, which is why water is a window seen from above and a mirror seen along it. The
    /// reflectance written as its value, for the reason `portable.h` gives.
    const float WATER_IOR = 1.333f;
    const float WATER_F0 = 0.020373188f;

    /// The most radiance the sun's disc is drawn with.
    ///
    /// **A ceiling for a temporal history, not for a picture.** The sun's disc is drawn at its
    /// irradiance spread over its own solid angle, which at noon is a hundred and seventeen thousand
    /// times the sun's irradiance. The dimmest exposure the renderer will choose is `EXPOSURE_MIN`, so
    /// a radiance of its inverse is already the top of the display range at every exposure it can
    /// pick. What the number does reach is the upscaler, which has to hold that value in a history,
    /// and a neighbourhood five orders of magnitude out of range clears slowly: a blown pixel that
    /// stays blown for seconds after the sun has left the frame.
    ///
    /// **Over `WATER_F0`, because a glint is the dimmest thing this can reach.** Water reflects that
    /// share of what it faces at normal incidence, so a source has to survive it and still clear the
    /// display's top: the smallest ceiling that leaves every white pixel white.
    ///
    /// **The disc alone, because it is the only thing in the sky that can reach a ceiling**: a moon's
    /// face is held at 0.18, a star at the same, and the dome's glow is three orders below this.
    ///
    /// `1 / (EXPOSURE_MIN * WATER_F0)`, written as its value for the reason `portable.h` gives.
    const float MAX_SUN_RADIANCE = 9816.824f;

    /// Extinction per world unit, per channel — how fast water swallows light along a path.
    ///
    /// **Absorption, and not a diffuse attenuation coefficient.** `Kd` counts scattering as a loss and
    /// counts the lengthening of a path that has been scattered about, but this renderer already puts
    /// the scattering back with `VisibilityConstants::mWaterScatter`, so charging the beam for it as
    /// well is charging it twice. What is left of a beam is what was absorbed out of it, which is `a`.
    ///
    /// **Scattering takes almost nothing out of a beam here.** The reduced coefficient is
    /// `a + b (1 - g)`, and with `WATER_ASYMMETRY` at 0.92 and the albedo below, `b (1 - g)` comes to
    /// between 0.3 and 1.2 per cent of `a`, so the term is named rather than carried.
    ///
    /// **Blue last, which is what the albedo beside this one already says.** Pure water absorbs red
    /// twenty-five times as fast as blue, and a scattering albedo that peaks in blue says blue is what
    /// survives to be scattered. With green surviving longest the two constants describe two
    /// different waters and the extinction wins: every path of any length read green.
    ///
    /// Two terms, per metre, over the visible band weighted by each channel's own response:
    ///
    ///   pure water, Pope and Fry     0.260, 0.054, 0.010
    ///   dissolved organic matter     0.002, 0.005, 0.014     `a(440) = 0.02, exp(-0.014 (l - 440))`
    ///
    /// The second is what makes this a coastal sea rather than an ocean, and it is the one dial:
    /// stained water absorbs blue and nothing else much. Every expectation a test makes about water
    /// derives from this sum.
    ///
    /// `(0.262, 0.059, 0.024) / UNITS_PER_METRE`, a metre's extinction in each channel, written as
    /// its values for the reason `portable.h` gives: `RtxSharedConstantTest` says the new ones when
    /// the per-metre figures change.
    const vec3 WATER_EXTINCTION = vec3(0.0037433251f, 0.00084296253f, 0.0003429f);

    /// The single-scattering albedo: the share of extinction that was scattering and not absorption,
    /// and so the part the water hands back as its own colour instead of swallowing.
    ///
    /// **This is what decides whether deep water is dark.** A channel whose scattering albedo
    /// approaches one settles at a bright colour however deep it gets, a milky sheet; a
    /// tannin-stained coastal swamp does not, and this game's water is the second.
    ///
    /// **The content's own, which it states as a colour rather than as an albedo.**
    /// `MWRender::FogManager::getFogColor` mixes `Water_UnderwaterColor` into the weather's fog at
    /// `Water_UnderwaterColorWeight`, and the water's share of that is the colour the game settles
    /// its own murk at. The trace reads the two fallbacks (`VisibilityConstants::mWaterScatter`), so
    /// a water mod's colour reaches it, and not the air's share, which is no property of the water.
    /// This is what Morrowind's own `012,030,037` at 0.85 come to.
    ///
    /// **What the game states and this cannot use is the density.** `Water_UnderwaterDayFog` is 2.5,
    /// and `FogManager` runs its ramp from `min(view, 7168) * (1 - depth)`, which for any depth over
    /// two starts *behind* the camera. A medium is nought at nought distance by construction, so
    /// `Rtx::fogExtinction`'s half-life match has nothing to bite on: that number is a screen tint
    /// rather than a density, and the absorption above is already the stronger by 7168 units.
    ///
    /// **It peaks in blue, and `WATER_EXTINCTION` is written to agree with it.** A blue-peaked albedo
    /// and a blue-sparing absorption are one statement about one water: move either and the other
    /// has to move with it, or the extinction is the one that shows.
    ///
    /// **What this asks of the scattering coefficient is not a real water's, the price of keeping the
    /// game's own number.** Read as an albedo it implies `b = a w / (1 - w)`, which falls toward blue
    /// where every real water's rises. What that costs is confined to the colour a very deep column
    /// settles at, which is the one thing the game states outright.
    ///
    /// `(12, 30, 37) / 255 * 0.85`, written as its values for the reason `portable.h` gives.
    const vec3 WATER_SCATTER_SHIPPED = vec3(0.04f, 0.1f, 0.123333335f);

    /// How far forward water throws what it scatters.
    ///
    /// **Sea water scatters forward far harder than fog does.** Petzold's measurements of coastal
    /// water give a mean cosine of about 0.92: nearly everything goes on in the direction it was
    /// travelling, so an underwater haze is a beam around the sun rather than an even milkiness.
    const float WATER_ASYMMETRY = 0.92f;

    /// The waterline, over which water with nothing under it becomes the shore beside it.
    ///
    /// Where the ground rises to meet the surface the depth between them goes to zero, and a pixel of
    /// water with no water in it has to come out as the ground, or the plane cuts the terrain along a
    /// hard line (on screen in 533 of the game's 1,292 land cells). Half a metre hides the
    /// intersection without making the shallows look thin.
    ///
    /// **Measured straight down and not along the refraction.** At Seyda Neen's shore the terrain
    /// runs within a few units of sea level for hundreds of units, while the refracted ray, leaving
    /// at forty degrees off the vertical, lands far enough out to find a bed well down: deep water
    /// reported at a pixel with none, and the fade never engages.
    /// `theWaterlineIsAsDeepAsTheWaterOverItAndNotAsFarAsARayThroughItGoes` holds it.
    const float WATER_SHORE_FADE = 35.0f;

    /// The scale of the pattern at the focus, in world units, which it grows from.
    ///
    /// **Snyder and Dera's other half, and the one a blur cannot supply.** The dominant frequency of
    /// the fluctuation falls as the inverse square root of the depth, so the pattern's own scale
    /// grows as the root of it, which is branching and not blurring. Both blur terms are linear in
    /// the depth and still under one texel at three metres.
    ///
    /// **Fitted against the law rather than derived.** The contrast from two metres to six comes out
    /// at 0.60 of itself here against the 0.58 the root asks for, and a larger grain overshoots it
    /// (0.36 at twelve). It lands within a texel of the wider tile, the finest the transform carries.
    const float WATER_CAUSTIC_GRAIN = 8.0f;

    /// How wide a patch of surface a point one unit down gathers its light from, per unit of depth.
    ///
    /// **Why a caustic coarsens as the water deepens.** The sun is a disc rather than a point and the
    /// surface presents a spread of slopes, both angles, so a point at depth `d` is lit by a patch
    /// this many units across, and reading the tiles at that footprint broadens the pattern as the
    /// water deepens. Both are linear in the depth, which is why they are not the whole of the
    /// coarsening: `WATER_CAUSTIC_GRAIN` carries the part that is not a blur.
    ///
    /// The sun's term is its angular *diameter*, narrowed by refraction on the way in. A mip chain
    /// preserves the mean, so nothing here changes how much light arrives.
    ///
    /// `2 * SUN_ANGULAR_RADIUS / WATER_IOR`, written as its value for the reason `portable.h` gives.
    const float WATER_CAUSTIC_SPREAD = 0.0069827456f;

    /// The depth a sea's caustics are boldest at, in world units.
    ///
    /// **Measured rather than derived, because the sea this renderer synthesises cannot find its own.**
    /// A real ocean's curvature is dominated by waves far shorter than `sShortestWave`, so its first
    /// focus lies within a metre of the surface (Snyder and Dera, 1970, and every field measurement
    /// since). The transform stops at half a metre of wavelength, so left to itself it focuses at
    /// eight, and a bed at six metres came out bolder than one at two. A metre and a half here, the
    /// shallow end of what the measurements report.
    ///
    /// **The carried pattern is normalised to reach its own fold here**, the other half of saying
    /// the sea is band-limited: the tiles hold about a fifth of a real sea's curvature, so at the
    /// literal deflection they would draw a pattern a fifth as bold at every depth. Scaling so the
    /// fold lands at the focus gives the light the strength it is measured to be redistributed with.
    const float WATER_CAUSTIC_FOCUS = 100.0f;

    /// How much of the pattern is drawn, as a share of its own departure from a flat sea.
    ///
    /// **The one number here that answers taste rather than a measurement.** What the arithmetic
    /// gives is the whole lens, and the whole of it reads brighter on a Morrowind shore than the
    /// game wants.
    ///
    /// **It scales the departure from one and never the light.** `causticGain` makes the pattern
    /// average to exactly one, so this can be turned anywhere between nothing and the full lens
    /// without the bed receiving a photon more or less than falls on the water.
    ///
    /// The ceiling is not this dial and cannot be: cutting the cusps lower makes `causticGain`
    /// divide by less, which puts the peak straight back.
    const float WATER_CAUSTIC_STRENGTH = 0.4f;

    /// How fast the pattern fades past the focus, as a power of the depth.
    ///
    /// **A half is what the sea was measured at.** Snyder and Dera's law is that the amplitude of the
    /// fluctuation and its dominant frequency both fall as the inverse square root of the depth, and
    /// that is what a measurement of the ocean says. One is twice that exponent, so the pattern is gone
    /// by twenty metres where the water still has light in it — chosen for the look and not found in
    /// the sea, which is worth saying out loud beside a file full of numbers that were.
    ///
    /// **Blending toward one rather than scaling is what keeps the light wherever this is set**, so the
    /// exponent is free to be turned and the mean does not follow it. Measured at two, six and twenty
    /// metres: 0.213, 0.064 and 0.014 of contrast, where a half leaves the deep end four times bolder.
    const float WATER_CAUSTIC_FADE = 1.0f;

    /// How far toward its own fold the pattern is run at the focus, as a share of the way there.
    ///
    /// **Past one, which is past where a lens has one answer.** At one the determinant first reaches
    /// zero; beyond it the map folds over and a point on the bed is reached by three patches of
    /// surface where this draws one. That puts the contrast into thin bright filaments, and this is
    /// the dial for how thin.
    ///
    /// **Conservation is not what limits it**: `causticGain` divides out the mean of whatever the
    /// estimator makes at a fold. What the fold costs is coherence: a filament is made of the
    /// fastest-turning waves, so it is what moves first.
    const float WATER_CAUSTIC_FOLD = 3.0f;

    /// What share of what a stretch of water sends the sun's own beam has to be before its shaft is
    /// drawn, and where the shaft reaches full strength.
    ///
    /// **A share and not an angle, which is the same test `fogAlong` makes.** What decides whether
    /// the pattern can be *seen* is the beam against the sky scattered beside it, which turns with
    /// the hour, the weather and the depth. Gated at an angle the shafts are there only when the sun
    /// is looked straight at; against this they reach as far as they are worth reaching.
    ///
    /// **Two of them, because one drew a circle.** A march that begins at a threshold begins with a
    /// pattern already in it, and the ring where it started was the sharpest edge in the frame. The
    /// pattern fades in across the two, and the ratio `ColumnRun` takes across them makes *nothing
    /// to show* come out as exactly the closed form.
    const float WATER_SHAFT_FLOOR = 0.04f;
    const float WATER_SHAFT_SHOWN = 0.15f;

    /// How many samples a shaft is drawn from.
    ///
    /// The pattern varies along the ray at the scale the surface's own lens does, so the steps are
    /// even rather than bunched: unlike the air, no density falls off with height for them to follow.
    ///
    /// **Four, and the beam's body does not depend on it.** The march is a ratio against the same
    /// steps without the lens, so the step count cancels out of everything but the pattern's own
    /// quadrature, and each step is a shadow ray. Against eight at the shore: 0.23 ms off the trace,
    /// twelve per cent of it, for a reference that moved by a hundredth of a per cent of its mean.
    const uint WATER_SHAFT_STEPS = 4u;

    /// How far apart the rain's impacts are, in world units: a lattice with one splash a cell.
    ///
    /// **How many rings is not how many drops.** A real rain lands thousands of drops a second on a
    /// square metre and a surface cannot show them as separate rings; what an eye picks out is a few
    /// tens. Twenty units is a dozen impacts on a square metre.
    const float RAIN_RING_CELL = 20.0f;

    /// How long one ring lasts before it has spread into nothing, in seconds.
    const float RAIN_RING_LIFE = 0.6f;

    /// How fast a ring spreads, in world units a second.
    ///
    /// Capillary-gravity waves cannot travel slower than 0.23 m/s, and a splash ring runs out at
    /// about twice that. Thirty-five units is half a metre a second, so a ring reaches thirty
    /// centimetres before its life is up.
    const float RAIN_RING_SPEED = 35.0f;

    /// The ring's own wavelength, in world units: eleven centimetres, the scale capillary ripples take.
    const float RAIN_RING_LENGTH = 8.0f;

    /// How steep a fresh ring is, as slope at its crest.
    ///
    /// Per ring, and rings overlap — nine cells are summed — so what it comes to as a field is what is
    /// compared against the sea: an rms slope of about a fifth, a third of a running sea's. Enough to
    /// break a reflection where a drop lands, and gone again within the ring's life.
    const float RAIN_RING_STEEPNESS = 0.30f;

    /// Where a sprite's rim starts, as a share of its radius, for the taper `spriteTaper` puts back
    /// on a disc the mip chain averaged into its own square.
    ///
    /// Six tenths leaves the blob an artist painted alone and rounds off what a texture's border
    /// became at the levels a spark is read at; earlier and the rim eats the flame, later and a
    /// distant spark is a little rectangle again.
    const float SPRITE_TAPER_START = 0.6f;

    /// The most a texel of a sprite may hide of what is behind it.
    ///
    /// **An alpha of one is an infinite optical depth, and no chord can thin one.** A sprite is
    /// composited as a ball the ray crosses, and what it hides is `1 - (1 - alpha) ^ fraction` for
    /// the fraction of the chord the eye sees, so at an alpha of one any sliver hides everything: a
    /// puff clipped hard at the wall it was meant to fade into. A texel that says opaque is taken
    /// to mean this, and the price is one part in a hundred of the background.
    const float SPRITE_ALPHA_LIMIT = 0.99f;

    /// How strongly smoke throws the sun forward: Henyey-Greenstein's asymmetry.
    ///
    /// **A puff lit by an even share from every side is a card, and a puff is not a card.** A cloud
    /// of droplets sends most of what it scatters on along the light, so smoke between the eye and
    /// the sun glows and smoke with the sun behind the eye is dim. Applied to the sun alone and
    /// normalised so the mean over every direction stays the card's worth; the sky and the lamps
    /// arrive from everywhere and keep the even share. Six tenths is the cloud recipe's figure.
    const float SMOKE_ANISOTROPY = 0.6f;

    /// How much brighter the lit side of a puff is than its mean, and the far side darker.
    ///
    /// **A puff has no dark side and still has a lit one.** A cloud of droplets scatters the sun
    /// through the whole of itself, which is why `puffLight` gives a puff a card's worth of the sun
    /// rather than a sphere's quarter; but the side the sun is on is brighter, and that makes a ball
    /// read as a ball. `1 + SPRITE_WRAP * dot(normal, toward)` keeps the mean over the sphere where
    /// it was and puts three to one between front and back.
    const float SPRITE_WRAP = 0.5f;

    /// The most a shell of medium may be thickened by the angle the ray crosses it at.
    ///
    /// **A painted alpha is what one crossing square to the shell hides, and a slanted crossing goes
    /// through more of it**: the same slab, `1/cos` as far through it, so the alpha becomes
    /// `1 - (1 - a) ^ (1/cos)`. Head on the content's own number is kept exactly.
    ///
    /// **It is unbounded at the limb**, where a ray runs along the shell, and a secant that goes to
    /// infinity draws a hard opaque ring around every cloud in the game. Four is where a shell stops
    /// thickening: a crossing at fifteen degrees off the surface, well past where a shell's own
    /// curvature has taken over from its slant.
    const float MEDIUM_GRAZE_LIMIT = 4.0f;

    /// The longest history a pixel may keep, in frames.
    ///
    /// **This is the one dial on the trade the accumulator exists to make**, and it is a trade
    /// rather than a setting with a right answer: a longer history is a quieter picture and a later
    /// one. The estimator's error falls as `1/sqrt(n)`, so the return on each further frame is
    /// shrinking while the lag it costs is not, and lag on a bounce shows up as light sliding off a
    /// wall a moment after the lamp that lit it moved.
    ///
    /// **Thirty-two, half a second at sixty frames, and only the bounce pays the lag.** The lamps'
    /// direct light and the sky's are the shadow denoiser's, so a lamp that moves or goes out moves
    /// its own light at once. The history is the cascade's own output, so each frame it keeps is
    /// blurred again, and a longer one is quieter standing and further from the truth. At the
    /// guild's planter at night, 16, 32 and 64 frames give a frame noise of 1.28, 0.96 and 0.72 and
    /// a bias of 2.14, 2.30 and 2.42 levels of 255; thirty-two is under the bias the rooms had at
    /// sixteen while the lamps rode the wavelet, at two thirds of their noise.
    ///
    /// **A reach that narrows as the history grows did not get out of that trade**: ReBLUR's
    /// `1 / (1 + N)` (Zhdan, GTC 2020) of 28 pixels, and the same over `sqrt(N)`, lie on the curve
    /// the history length draws, the noise of the first in sparse bright points the narrowed cascade
    /// no longer spread.
    const float ACCUMULATE_FRAMES = 32.0f;

    /// The longest history the accumulator's fast mean keeps, which the slow one is clamped to: the
    /// mean that follows a change of the light on a surface that did not move, as an actor's darkness
    /// dragged over a floor.
    ///
    /// **Two, and not ReLAX's six** (NVIDIA NRD, `diffuseMaxFastAccumulatedFrameNum`). Measured as
    /// the sky's trail behind a moving bar (`RtxBounceTrailTest`) and, in a room lit by what glows
    /// in it (`akulakhan-chamber`), the still and strafed frame's bias:
    ///
    ///     2     7.86 px   1.90 / 2.37
    ///     6    12.59 px   1.54 / 2.84
    ///
    /// A longer fast mean darkens a standing eye's frame less and a moving one's more, and drags the
    /// trail; the lit rooms of the bounce suite stand within 0.01 at all three tried. A shorter one
    /// is a wider box around the pixel's own light, so it holds the slow mean to the change sooner
    /// and to noise no more. One would be the sample itself, which the clamp's comment says why not.
    const float ACCUMULATE_FAST_FRAMES = 2.0f;

    /// How many deviations of the fast mean, over a 5×5 square, the slow mean may stand from the
    /// square's mean of it before it is held to that edge: ReLAX's `fastHistoryClampingSigmaScale`.
    const float ACCUMULATE_CLAMP_SPREAD = 2.0f;

    /// How hard a clamped pixel's two means are pushed on toward the frame's 5×5 mean of samples:
    /// the luminance of the gap between them, times this and the share the clamp moved, as a
    /// distance along the way, never past it. ReLAX's `accelerationAmount` of 0.3, scaled by its 10.
    /// Under the reuse it took the sky's trail behind a moving bar from 14.24 pixels to 13.07.
    ///
    /// **ReLAX's reset is not taken.** It blends both means toward the sample where the slow one
    /// stands from the samples' mean by more than 4.5 deviations of the fast mean and half of the
    /// samples': on this tree's bounce, whose samples are one bounce each, that never happened.
    const float ACCUMULATE_ACCELERATION = 3.0f;

    /// How far either way of a pixel the ring of fast means reaches that its slow mean is held
    /// under, and how many of the ring's deviations over the ring's mean it may stand
    /// (`ringHeldLuminance`). ReBLUR's anti-firefly (NVIDIA NRD, `REBLUR_HistoryFix`) at its
    /// defaults: `REBLUR_ANTI_FIREFLY_FILTER_RADIUS` and `REBLUR_ANTI_FIREFLY_SIGMA_SCALE`, over the
    /// 9×9 square less the 3×3 at its centre, so a firefly that the reuse or the history fix spread
    /// over a few pixels does not hold itself up.
    const uint ACCUMULATE_RING_REACH = 4u;
    const uint ACCUMULATE_RING_HOLE = 1u;
    const float ACCUMULATE_RING_SPREAD = 2.0f;

    /// The longest history the ring holds down: a slow mean of more frames keeps what it holds.
    ///
    /// **What the ring holds down is light, and a settled history pays for none of it.** The ring's
    /// 72 fast means of two frames hold 144 draws, so an event of one in a thousand is in none of
    /// them on six frames in seven, and the ceiling cuts it from the slow mean that took it in.
    /// ReBLUR holds every pixel, and on the M[FR] guild's tree, where the bounce finds a lantern's
    /// glowing paper about that rarely, the still frame stood 4% darker for it. The ring is for a
    /// mean of a few frames, where one such bounce is a blotch the size of a leaf; a mean of more is
    /// a still picture's, and comes to the truth. Against 2048 frames averaged on that tree strafed
    /// in (`noise --strafe=150`), eight frames cut the fireflies from 1.15 to 0.04 in a thousand
    /// and the noise from 3.70 to 1.85, at a mean of 0.0138 against the truth's 0.0154.
    ///
    /// **Under the upscaler a still frame is not all settled**: its jitter keeps a short history at
    /// every edge, and the ring holds those. On the bounce suite the ring takes less noise
    /// everywhere a lamp lights, and more bias, most where paper lanterns glow: the yurt's glow is
    /// counted by the bounce beside their lamps (`landingLightOf`), and the reference counts it too.
    ///
    /// **Off by default** (`ReconstructionRequest::mAntiFirefly`). On vanilla content the ring holds
    /// no firefly the count sees, and in a room lit by what glows in it (`akulakhan-chamber`, its
    /// lamps off) it took that light with it: the still frame's bias 2.60 with it and 1.90 without.
    ///
    /// **What it costs is the ring's square and not its sum.** Passing over the 81 taps where the
    /// history is long saved nothing: a pixel pays for the 16×16 tile its workgroup loads where the
    /// anti-lag's takes 12×12, and the barrier between the two. **So off is a pipeline without it**
    /// (`ACCUMULATE_CLAMP_SPEC_RING`) and not a factor of nought, which paid for the tile.
    ///
    /// **ReBLUR's other guard is not taken**: its temporal accumulation holds a blend under `2 + 38
    /// / (n + 1)` times the history of `n` frames it blends into (`REBLUR_TemporalAccumulation`).
    /// It held the still frame 2.7% darker, for ever: a ceiling over a settled history is the ring's
    /// bias at every length.
    const float ACCUMULATE_RING_FRAMES = 8.0f;

    /// How squarely two normals must agree before their pixels are the same surface, and the
    /// history at one may be carried to the other.
    ///
    /// A cosine and not the exponent the cascade uses: this is a yes or a no about whether to carry
    /// a history at all, where the cascade is weighing how much of a neighbour to take.
    const float ACCUMULATE_FACING = 0.9f;

    /// How far off a pixel's plane a history texel may stand and still be its surface, as a share of
    /// the frustum's narrower side where the surface stands: ReLAX's disocclusion threshold, and
    /// its default.
    ///
    /// **Off the plane and not along the ray**, so the far ground at a grazing angle, which steps a
    /// long way in distance from one texel to the next, is one surface to its history as it is to
    /// the wavelet (`ATROUS_PLANE_SIGMA`). **A share of the frustum, so the rule is one at every
    /// resolution and every range**: at a field of sixty degrees, a hundredth of the side is 1.2% of
    /// the distance, inside a wall's thickness at arm's length.
    const float ACCUMULATE_PLANE = 0.01f;

    /// How far from a pixel the accumulator looks for a surface nearer the eye that carries the
    /// motion of what hid it last frame, in pixels (`occluderMotion`): rings at 1, 2, 4, 8 and 16,
    /// since the occluder stands as far off as it moved against what it uncovered.
    const int ACCUMULATE_DUAL_REACH = 16;

    /// How near the occluder's motion, read where that motion says the occluder now stands, has to
    /// land to where the pixel's own lands, in pixels: half a pixel of rounding each way and half
    /// of slack.
    const float ACCUMULATE_DUAL_MISS = 1.5f;

    /// How many frames a pixel needs before its second moment describes a spread rather than a
    /// coincidence.
    ///
    /// **Under this the cascade is told the pixel is as uncertain as a pixel can be**: a mean of two
    /// samples has a variance, and it is not one anybody should filter by.
    const float ACCUMULATE_SETTLED = 4.0f;

    /// How much a slow mean of very few frames has its spatial variance raised by: `max(1, 4 / (n +
    /// 1))` for a mean of `n` frames (`shortHistoryVariance`), ReLAX's own figure for its spatial
    /// variance estimate (NVIDIA NRD, `RELAX_AtrousSmem`): twice the spread around a fresh pixel,
    /// and the spread itself from three frames on. A mean of one frame is one sample, which stands
    /// further from the truth than its neighbours' spread says, since they are as noisy as it.
    const float ACCUMULATE_VARIANCE_BOOST = 4.0f;

    /// The longest history the wavelet's first level rebuilds from the surface around it, in
    /// frames: NVIDIA NRD's history fix (`historyFixFrameNum`), whose three this is.
    ///
    /// **Where the eye uncovers a surface or brings it in at the frame's edge**, its mean is one
    /// bounce, and the cascade's sixteen pixels spread that one bounce into blotches that settle
    /// over the frames after it. Rebuilt instead from the surrounding surface under ReLAX's flat
    /// kernel, a pixel shows the light of that surface and starts its history from it. Strafed and
    /// walked into the bounce suite's rooms it cut the noise by up to a fifth, and the bias rose by
    /// 0.10 at most (`noise --suite=bounce`).
    ///
    /// **What the flat kernel costs is light, where light rises across the surface it borrows
    /// from**: the mean of fourteen pixels either side of light that rises faster than in a line
    /// stands over the light at the centre. Under a lamp's spot on a wall
    /// (`theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo`) the strip the eye turned to reads 1.22 of
    /// its light fourteen columns in from the edge and 1.52 at it, for three frames at most.
    ///
    /// **Not ReBLUR's weight of each tap by the frames its own mean holds**, which held the strip 1.44
    /// times as far over its reference at the frame's edge. **Nor the B3 spline the first level
    /// filters by**, though it halves the kernel's second moment: it moved no bias by more than 0.03,
    /// and every place was noisier one and two frames after a cut.
    const float ACCUMULATE_FIX_FRAMES = 3.0f;

    /// How far apart the history fix's 5×5 taps stand for a history of `n` frames, in pixels:
    /// `ACCUMULATE_FIX_STRIDE / (1 + n)` rounded, NRD's `historyFixBasePixelStride` and its rule —
    /// seven pixels for a new pixel, so its taps reach fourteen, and four once three frames hold.
    const float ACCUMULATE_FIX_STRIDE = 14.0f;

    /// What share of the lobe's volume the glossy filter's lobe angle holds: ReLAX's
    /// `GetSpecLobeTanHalfAngle` default, whose half angle is `atan(r² · v / (1 - v))` for a
    /// perceptual roughness `r` — three `r²` at this share.
    ///
    /// **ReLAX's, because it is the one published rule for how far a view may turn before a glossy
    /// history describes a different reflection**, and the constant is what that rule was tuned at.
    const float SPECULAR_LOBE_VOLUME = 0.75f;

    /// The perceptual roughness under which the glossy filter's history shortens with the lobe:
    /// ReBLUR's responsive accumulation (NRD's `ReblurResponsiveAccumulationSettings`), which caps the
    /// frames at `ACCUMULATE_FRAMES · lerp(c, 1, smoothstep(r / this))`, `c` the specular curve
    /// `(1 - 2^(-200 r²)) r^¼`. **What bounds a sharp reflection's lag**: the view's turn drops a
    /// history only where the eye moves, and a lamp or a body moving before a still eye left a
    /// mirror's reflection thirty-two frames behind it. NRD ships it off and names animated water as
    /// its use; here no fast history bounds the lag instead, since a two-frame fast mean clamping
    /// the slow one took a still eye's filtered means off the mean of their frames. At a quarter:
    /// 0.05 keeps seven frames, 0.02 three, and 0.15 twenty-seven.
    const float SPECULAR_RESPONSIVE_ROUGHNESS = 0.25f;

    /// The fewest frames a responsive history keeps, NRD's default: a mirror's reflection is a sample
    /// of one lamp drawn among several, and fewer frames than this leave that draw in the picture.
    const float SPECULAR_RESPONSIVE_FRAMES = 3.0f;

    /// The most either upscale mask says of a pixel (`CHANNEL_UPSCALE_MASKS`). **The FSR 3.1 guide's
    /// own advice**: "it is unlikely that a reactive value of close to 1 will ever produce good
    /// results … we recommend clamping the maximum reactive value to around 0.9". At one the
    /// upscaler keeps nothing of its history and shows the jittered sample alone.
    const float UPSCALE_MASK_CEILING = 0.9f;

    /// How far off the centre pixel's tangent plane a tap may sit, in pixel footprints.
    ///
    /// **Off the plane, not away from the eye.** Terrain seen at a grazing angle steps a long way in
    /// distance between neighbouring pixels while remaining one flat surface, so a test on distance
    /// alone would refuse to filter exactly the ground that most needs it. Measured in footprints
    /// where SVGF divides by a depth gradient a ray tracer has no rasterizer to hand it.
    const float ATROUS_PLANE_SIGMA = 2.0f;

    /// How far a tap's brightness may differ from the centre's before it stops being the same
    /// light, in standard deviations of what the centre and the tap have been measuring.
    ///
    /// **The term that wants a history**, because a variance is taken from one. With it the filter
    /// can stop at an edge in the *light*, the line where a shadow ends on a flat wall, which the
    /// normal test and the plane test both read as one surface. Scaled by the estimator's own
    /// spread, so a pixel that is still noisy filters widely and a settled one holds its detail.
    /// SVGF's own figure.
    ///
    /// **The variance of both, and not the centre's alone as SVGF and ReLAX (`RELAX_AtrousSmem`)
    /// take it.** Where a bounce finds a bright surface rarely, a pixel whose history never found it
    /// has a variance of nought and refused every brighter tap, while one that found it took its
    /// dark taps in: light left the bright pixels and reached no dark one. A floor lit by a
    /// lamp-lit spot on a wall kept 0.969 of its light under the centre's alone and 0.988 under both
    /// (`theHistoryFixTakesTheNoiseOffWhatTheEyeTurnsTo`). It costs the square root at every tap:
    /// the filter's median rose by 0.005 to 0.015 ms of 0.66 to 0.70.
    ///
    /// **The tap's own variance as the level reads it, and not prefiltered as the centre's is**
    /// (`varianceAround`): prefiltered at the wide level, every place held its noise and its bias to
    /// 0.01, and the filter rose from 0.58–0.82 ms to 1.42–1.87.
    const float ATROUS_LUMINANCE_SIGMA = 4.0f;

    /// How many levels the cascade runs, its taps standing 1, 2, 4 and 8 pixels apart.
    ///
    /// **A 5×5 first level and three 3×3 after it reach sixteen pixels**: the first takes two taps
    /// at its spacing and each later one a tap at its own, so the support is `2 + 2 + 4 + 8`: the
    /// à-trous trick, where the holes between taps grow while the tap count does not. SVGF's B3
    /// first level, then ReLAX's later levels (`RELAX_Atrous`): they weigh by the centre's variance,
    /// which the levels before already averaged, and skip the prefilter's nine loads.
    ///
    /// **Not the five SVGF runs, because what the wide levels did is done over time now.** The
    /// accumulator averages `ACCUMULATE_FRAMES` ahead of the cascade and FSR accumulates behind it,
    /// so a level past the reach of sixteen spreads light that is already quiet. Three 5×5 levels
    /// stood level with five at native; these four take the filter's share of the frame from 1.09 ms
    /// to 0.69 at the guild under FSR quality at 1920×1080, and move no place of the bounce suite by
    /// more than 0.01 of noise or bias.
    const uint ATROUS_LEVELS = 4;

    /// The step past which a level's eight outer taps stand off their lattice, by a hash of the pixel
    /// and the frame, under a quarter of the step either way and truncated toward nought: ReLAX's
    /// (`RELAX_Atrous`), "to minimize ringing at large A-Trous steps". A fixed 3×3 lattice eight
    /// pixels apart leaves the grid it samples on the picture, the à-trous artefact; moved per pixel
    /// and per frame it averages away over time. The last level alone, whose step is eight. On the
    /// history fix's scene the noise of two pixels eight apart correlated at 0.645 and correlates
    /// at 0.424 (`theWidestLevelsTapsStandOffTheirLattice`). **It costs about 0.05 ms of the median
    /// frame**, where neighbouring lanes fetch taps the offset scattered.
    const uint ATROUS_JITTER_STEP = 4u;

    /// How much of a froxel's answer comes from where it stood last frame.
    ///
    /// **Heavy, because what it is averaging is one jittered sample.** A froxel takes one point out of
    /// its volume, one shadow ray each for the sun, the moons and the lamp it held, and the mean of
    /// those draws is the answer. Nine tenths converges over about ten frames, a sixth of a second:
    /// slow enough to hide the draw and quick enough that a shaft swinging open is not a fade.
    const float FOG_VOLUME_HISTORY = 0.9f;

#ifdef RTX_HOST
}
#endif

#endif
