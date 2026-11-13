#ifndef OPENMW_COMPONENTS_RTX_SHADERS_VISIBILITY_H
#define OPENMW_COMPONENTS_RTX_SHADERS_VISIBILITY_H

#include "camera.h"
#include "hosttypes.h"
#include "look.h"
#include "portable.h"
#include "scene.h"
#include "sky.h"
#include "wave.h"

// Included verbatim by both the shader and the C++ that fills it in, so the two cannot disagree about
// a field: scalar block layout makes a `vec3` twelve bytes on both sides, with none of std140's
// padding rules. `<cstddef>` is for the `offsetof` the pinned layout below is checked with.
#ifdef RTX_HOST

#include <cstddef>

namespace Rtx::Shaders
{
#endif

    /// What a pixel is painted with: the light, or one input of the surface written straight out —
    /// `Rtx::SurfaceView`, which says what each is.
    const uint SHOW_SHADED = 0u;
    const uint SHOW_ALBEDO = 1u;
    const uint SHOW_NORMAL = 2u;
    const uint SHOW_ROUGHNESS = 3u;
    const uint SHOW_SPECULAR = 4u;

    /// What the frame is: where the eye stands, how it turns a pixel into a ray, and everything about
    /// the world that a ray needs to be answered. **The ray generator is `Camera` and is separate**,
    /// because the wavelet builds the same rays and needs none of the rest.
    struct VisibilityConstants
    {
        vec3 mOrigin;

        /// How a pixel becomes a ray, through the world's eye or the arms'. The eye's own place is
        /// `mOrigin` above and not in here, for the reason `Camera` gives.
        Eyes mEyes;

        /// How much wider the arms' image plane is than the eye's, per axis — one where the two fields
        /// of view are equal, as shipped. What a point on the arms reprojects through, worked out once on
        /// the host.
        vec2 mArmsSpread;

        /// The eye's right and up at unit length, where `mEyes` carries them scaled by the image plane's
        /// half extents: the eye space a sphere-mapped sheet is indexed in, worked out once on the host.
        vec3 mUnitRight;
        vec3 mUnitUp;

        /// Where the debug lines' depth range starts, in world units from the eye. **A ray tracer has
        /// no near plane**: nothing the trace does clips against it, and the lines, which are
        /// rasterized, are the one reader.
        float mNear;

        /// How far the camera's own ray travels: the clip. The world's own size for the world's camera,
        /// where a primary ray that reaches it has left the world; a picture's framing for a picture,
        /// which clips at the depth range it frames.
        float mFar;

        /// How far every other ray travels before whatever it was looking for counts as not being there:
        /// the shadow rays, the ambient ray, a bounce, a reflection. `Rtx::sFarPlane` for every camera,
        /// because what lights a point is the world around it and not how near a picture of it clips: a
        /// map tile that clipped its shadow rays at its depth range lit the ground under every roof.
        float mReach;

        /// What a pixel is painted with — `SHOW_*`, `Rtx::SurfaceView` — where it is not the light.
        uint mShow;

        /// Non-zero where there is no sky behind the subject: a ray that hits nothing comes back with no
        /// radiance and the whole of the backdrop shown, so the pixel is the interface behind the
        /// picture rather than the horizon's colour. **What a picture inside the interface is**: the
        /// inventory doll and a map tile are composited over the window, so what they do not cover has
        /// to be nothing (`BACKDROP_INTERFACE` is how the curve says so). **No air and no water in such
        /// a picture** (`PictureTracer` asserts): the backdrop is one number a pixel and a medium in
        /// front of it would ask for three.
        uint mTransparentBackground;

        /// Non-zero where no lamp lights the picture: a view whose mask leaves out `Mask_Lighting`,
        /// as the local map's does, which the rasterizer's light manager hands no lamp. Zero for a
        /// frame, and for a frame built by hand.
        uint mNoLamps;

        /// Non-zero where the sky's lights cast no shadow: every picture inside the interface, which
        /// the rasterizer draws with shadows off (`disableShadowsForStateSet`) under its own flat
        /// sun. Zero for a frame, and for a frame built by hand.
        uint mNoSkyShadows;

        /// The sun as a light: where it stands, unit; how much of its light arrives on a surface square
        /// to it; and the sine of the cone its shadow rays are drawn from, which is
        /// `SUN_SHADOW_RADIUS`'s and not the disc's own half degree.
        ///
        /// **The same record a moon lights with**, so a surface and a froxel weigh the three sources by
        /// one rule. **One direction and not two**, so `-mDirection` is where the light travels: the
        /// game gives its light a fixed climb and its disc a height of `swing - |east|`, and a tracer
        /// answering to each gets a different sun in the shadows, the water and the haze.
        /// **One test for whether there is a sun at all: `mIrradiance` is zero** (an interior, a night,
        /// either end of the day), and every use of the sun is gated on it; `Rtx::makeSkylight` keeps
        /// that true. The direction stays meaningful through the night, as a moon's crescent points at
        /// where the sun would be.
        SkySource mSun;

        /// What the disc is painted with, linear. **The hue is not the sunlight's**: a weather's
        /// sunlight is the sky's colour as much as the sun's (`Sun_Night_Color` is a blue no sun ever
        /// was) and the ramp is still crossing through dawn, so a disc tinted by it comes up blue.
        /// Morrowind records the disc's own colour, white until the sun starts down. How much of it
        /// there is is the irradiance's, so a drawn disc and a cast shadow are the same fact.
        vec3 mSunDiscColour;

        /// The cloud deck and the star field over it, and the nebulae and constellations painted
        /// across that. They all fade together on `StarField::mFade`, because in the engine they are
        /// one mesh under one switch.
        CloudDeck mClouds;
        StarField mStars;
        SkyPatch mSkyPatches[SKY_PATCH_COUNT];

        /// What a ray that hits nothing comes back with, at the horizon and overhead, and where the one
        /// fades to the other: the game's atmosphere overhead and the fog it fades to at the horizon.
        vec3 mSkyHorizon;
        vec3 mSkyZenith;
        SkyRamp mSkyRamp;

        /// Whether the sky is drawn for the eye and the mirrors. Nought under `tsky`, where the
        /// rasterizer hides the sky node and clears to the fog colour while the sky goes on lighting
        /// what it lit, so only what a ray that reached nothing shows changes. One in a frame built
        /// by hand.
        uint mSkyDrawn;

        /// How much of `mAmbient` arrives from the sky, from none of it to all. Out of doors the
        /// ambient is the sky; inside it is the cell's own `AMBI`, a fill standing for every bounce the
        /// room makes. Two answers hang off which.
        ///
        /// **Whether the sky is a light at all**: a room's dome is its fog colour standing in for the
        /// picture, and lighting anything with that draws a bright band along every wall's foot.
        /// **How far `ambientReaching` looks for what stands over a point**: out of doors the ray runs to
        /// the sky, so anything takes it away; in a room the walls *make* the fill, so only what is within
        /// `ROOM_FILL_REACH` does (a ray run to the walls comes back blocked everywhere, and occluding
        /// by nothing lets white cloth light its own contact shadow).
        /// **A frame assembled by hand is a room until it says otherwise**; `Rtx::describeWorld` writes
        /// this on every frame either host renders. One and nought are the only values written today.
        float mAmbientFromSky;

        /// The day's gain, `Rtx::Skylight::mDaylightGain`, for what the sky draws from a constant
        /// rather than from a term the host already lifted: a moon's face at `MOON_RADIANCE`. One in
        /// a frame built by hand.
        float mDaylightGain;

        /// What the sky lights with over and above those two, and is not drawn with: **the one place
        /// where what the sky sends and what it shows differ**. `Rtx::Skylight::mFill` carries why:
        /// Morrowind states a night's light as an ambient on every surface, an order above the colour
        /// it draws its night sky, and a renderer lighting by that sky is short by the difference.
        vec3 mSkyFill;

        /// Where the water's surface is, or negative infinity where the cell holds none: everything that
        /// asks does so as "how deep is this point", so a cell with no water takes the path of a point
        /// above the surface with no branch of its own.
        float mWaterLevel;

        /// The water's single-scattering albedo, the colour the game settles its murk at:
        /// `Water_UnderwaterColor` at its weight. `WATER_SCATTER_SHIPPED` says what it is.
        vec3 mWaterScatter;

        /// How long the water has been moving, in seconds, as two floats whose sum is the host's
        /// double: `Rtx::splitSeconds`, and `turnsAt` is what reads it. Nought is a still sea and a
        /// deterministic frame, which is what a test wants; the window path passes its own clock.
        vec2 mWaterTime;

        /// How hard it rains on the water, from nought to one: the precipitation's own alpha where its
        /// kind rings the surface (rain does, snow settles; `MWRender::Precipitation::
        /// getRainRipplesEnabled` off the ini's `Rain Ripples` and `Snow Ripples`), nought elsewhere —
        /// the number the rasterizer hands its water as `rainIntensity`. `rainSlope` reads it.
        float mRainOnWater;

        /// Which way the wind drives the sea, unit, in the world's XY. **The tiles are spread about
        /// their own +X, and this turns them**: a spectrum built for one heading would be built again
        /// for the next, while a rotation of where the tiles are sampled costs nothing and is exact. It
        /// is the deck's heading, as there is one wind over a landscape (the fog's too), and +X where
        /// nothing blows.
        vec2 mSeaHeading;

        /// How wide each of the sea's tiles is, in world units, in the order they are bound: what turns a
        /// world position into a texture coordinate and a cone width into a level. **Handed over rather
        /// than a shader constant**, so what the shader divides by is what `WavePass` built.
        float mWaveExtent[WAVE_CASCADES];

        /// How wide one texel of each of those tiles is, in world units, in the same order. **Handed
        /// over rather than asked of the driver**: `waveLevel` divides a footprint by this, and
        /// `textureSize` is a header fetch for a number `sWaveTiles` states at compile time, six times
        /// over in an underwater pixel. It is the extent above one division further on.
        float mWaveTexel[WAVE_CASCADES];

        /// Where the ripple field's window begins, in world units, and how wide it is: nought
        /// wide where no field is stood, which is every picture inside the interface and every
        /// frame before the first sea. `rippleSlope` reads nothing of a field nought wide.
        vec2 mRippleOrigin;
        float mRippleExtent;

        /// Root mean square slope of the whole sea, over every tile and wavelength. **A property of the
        /// sea and not of a place in it**, so one number and not a fetch: the caustic's band limit wants
        /// it (a point at depth `d` gathers from a patch `bend * d * this` across), and reading it off a
        /// chain's coarsest level would be two fetches at every step of a march for one constant.
        float mWaveSlope;

        /// Mean square of the sea's curvature trace, over every tile and wavelength: what the caustic's
        /// `bend` is sized against, so `WATER_CAUSTIC_FOLD` says how far the map runs whatever the sea
        /// state. **A property of the sea and not of a place**, as `mWaveSlope`.
        float mWaveCurvature;

        /// What share of `mWaveCurvature` a tile still resolves at a level of its chain, indexed
        /// `cascade * WAVE_LEVELS + level`. **The fold the caustic's gain is read at, and not one taken
        /// off the chain**: differenced per pixel it is noisy, with noise running with the very
        /// determinant it normalises; and a chain read answers for the *texels*, where the shader sees
        /// `textureLod` reconstructing between them, a second filter that costs a quarter of the
        /// curvature in the shallows and two thirds in deep water. `Rtx::waveCurvature` states both.
        float mWaveResolved[WAVE_CASCADES * WAVE_LEVELS];

        /// The cell's own ambient, linear, and what a path is terminated with. **Not added on top of the
        /// light that is traced**: Morrowind's interiors were authored against a renderer with no
        /// bounce, so this stood in for every one, and adding it to a surface that gathers a real
        /// hemisphere counts the same light twice. It sits one level down: a bounce that lands on
        /// something is shaded with this, so it estimates the rest of a path nobody traces.
        /// **Load-bearing indoors and marginal outdoors**: zeroing it halves an interior's mean
        /// luminance and barely moves an exterior's, whose second bounce mostly finds traced sky.
        vec3 mAmbient;

        /// What the surface the eye found takes where `mBounceRays` is nought and no path ends at a
        /// landing (`Rtx::Skylight::mSurfaceAmbient`).
        vec3 mSurfaceAmbient;

        /// What lights the water in front of the eye (`Rtx::Skylight::mWaterAmbient`): seen directly,
        /// with no surface's albedo in front of it, so a room's stands apart from `mAmbient`.
        vec3 mWaterAmbient;

        /// What the air between the eye and everything else scatters toward it, and how much of it there
        /// is. **The colour is the horizon's**: Morrowind records one colour for the fog and the sky's
        /// lower half because they are the same thing at two distances, so a ray that reaches nothing
        /// converges on what a ray through a mile of air does. An interior carries its own in `AMBI`.
        /// The extinction is absolute, per world unit, at the fog's base (the host turned the record's
        /// view-range-relative dial into one). Zero is no fog, which the tests that measure surface
        /// radiance need.
        vec3 mFogColour;
        float mFogExtinction;

        /// How much of the air is an even haze rather than banked: one in a room, and
        /// `FOG_EXTERIOR_EVEN` out of doors. **A room is not a small valley**: a cell smaller than one
        /// bank running the outdoor coverage field reads as a rendering fault. The two are mixed
        /// rather than branched, and as the banked field is normalised to average one, the mix changes
        /// the air's character and never how much of it there is.
        float mFogUniform;

        /// How deep the fog's layer stands, as a multiple of `FOG_HEIGHT`. **A weather with more fog has
        /// fog that reaches higher**: `Land Fog Depth` is 0.69 for clear and 1.9 for a foggy night, so
        /// foggy air fills a bay where clear lies in the hollows; without this every weather would pool
        /// in the same 37-metre bank. `Rtx::fogLift` derives it, and says why wind alone could not.
        float mFogLift;

        /// Where each scale of the fog's field is read from, coarsest first: an offset in that scale's
        /// own texture coordinates, from nought up to one. **Two motions in one offset.** The churn
        /// (`FOG_CHURN_COARSE` and the two beside it) drags the scales past each other on disagreeing
        /// headings, so shapes form and pull apart even in a dead calm. The drift carries the whole
        /// field downwind on the cloud layer's heading, as there is one wind over a landscape; it is
        /// how far the air has been carried, integrated over the clock (`Rtx::FogDrift`), because a
        /// wind times the clock jumps whenever the wind changes, and each scale takes it turned as that
        /// scale is. **Reduced on the host, in double, against each scale's tile** (`Rtx::fogOffsets`):
        /// both grow without bound over a session and the field repeats every tile, so no float holds
        /// ten hours of either.
        vec3 mFogOffsets[FOG_SCALES];

        /// How far from the eye the world is built, in units. Zero where nothing is cut off. **The
        /// second element of the air, and the one the weather knows nothing about**: Morrowind records
        /// how thick its fog is, not where this renderer stopped building ground, so the last cell
        /// ends in mid-air and the player sees the cut. The air closes over that ring and nothing
        /// nearer (`FOG_EDGE_RAMP`, `FOG_EDGE_RISE`).
        float mFogEdge;

        /// Masser and Secunda, in that order. An interface trace and an interior leave both at an
        /// alpha of nothing, which costs the sky one compare each.
        MoonDisc mMoons[MOON_COUNT];

        /// Where the scene's lamps were binned, folded in by the pass as the sea's tables are
        /// (`GpuLightGrid` says why it rides here and not in a table).
        GpuLightGrid mLightGrid;

        /// Where the eye stands now, less where it stood on the previous frame. **Differenced on the
        /// host**: world coordinates run to six figures and a motion vector is a fraction of a pixel,
        /// so subtracting two world points on the device loses the answer; two camera positions within
        /// a step subtract exactly in a float, and the device only adds that delta to an offset from
        /// its own eye.
        vec3 mCameraMotion;

        /// The previous frame's eye, with the translation left out (`mCameraMotion` carries where the
        /// eye was). All zero before there is a previous frame, which the shader reads as "no answer".
        Basis mPrevious;

        /// This frame's world eye and `mPrevious`, each read the other way (`screenOf`). The
        /// backend's, worked out where it writes this block from the bases beside them, so no
        /// block carries a basis and another's inverse; nought in what the core describes.
        ScreenBasis mScreen;
        ScreenBasis mPreviousScreen;

        /// Which frame this is, keying every random draw in the shader (the fog's step jitter, the
        /// bounce's direction): what makes two renders of one camera differ. Zero is a repeatable frame,
        /// which a test wants; a window passes its own count.
        uint mFrame;

        /// Non-zero where this scene holds the player's arms and this camera draws them:
        /// `visibility.comp` traces `Eyes::mArms`'s ray on `MASK_FIRST_PERSON` ahead of the
        /// world's, and a picture with no arms in it — every third-person frame, every picture
        /// inside the interface — pays no second trace.
        uint mArmsInFrame;

        /// Non-zero where the trace's sprite bin took a sprite, or a medium or additive placement:
        /// anything `puffLight` lights. The air's ambient ray is cast for nothing else, and a frame
        /// with none of them pays none — a fifth of what the air costs.
        uint mPuffsInFrame;

        /// Which classes of instance this camera draws — the rasterizer's cull mask, in the bits
        /// `scene.h` names. The eye's rays cast with it whole and every other ray with
        /// `solidMask` of it; `MASK_PARTICLE` in it is whether the sprites are drawn at all.
        uint mRayMask;

        /// How far over the eye a roof still keeps the rain off, in units, or nought for a frame with
        /// nothing falling that a roof stops: `spriteshelter.rgen` traces every falling sprite straight
        /// up to `mOrigin.z + mShelterHeight`, the top of the box `PrecipitationOccluder` draws its depth
        /// map from (the precipitation's own range and a cell's height over it).
        float mShelterHeight;

        /// How many columns and rows of froxels stand in front of the camera. **The froxel grid, said
        /// once**: `puffLight`, `fogVolumeAlong` and `fogVolumeWas` read it, and `textureSize` is a
        /// driver query for a pair of integers `FogVolume` holds on the host for the whole frame.
        uvec2 mFogColumns;

        /// How many of those a trace fills and integrates, `FogVolume::tracedFor`: the grid's own
        /// for the frame, and fewer for a picture smaller than the largest its chain was grown to.
        /// The integrate pass's tent stops at its edge, because a column past it holds the air of
        /// a picture traced before.
        uvec2 mFogTraced;

        /// Where the trace's per-pixel draws come from: `NOISE_BLUE_TILE` or `NOISE_WHITE_HASH`,
        /// as `Rtx::Reconstruction::mNoise` resolved it. One uniform branch in `randomAt`.
        uint mNoise;

        /// What every texture level is offset by, in levels: `Rtx::Reconstruction::mLevelBias`, the level
        /// bias for the pixel that is shown rather than the one that is traced; below nought under an
        /// upscaler that traces fewer pixels than it shows. Added where a level is chosen from a cone
        /// (`coneBase`, `waveLevel`, `rippleLevel`) and nowhere else, because `mSpreadAngle` also sizes
        /// the sun's disc and the wave filter's taps, which are not levels.
        float mLevelBias;

        /// How many taps the footprint sampler takes along a footprint's long axis at most:
        /// `Rtx::RenderProfile::mAnisotropy`, one where it reads no footprint. What `texturePoint`
        /// says the anisotropic read resolves, and so what level a normal map's loss is read at.
        float mAnisotropy;

        /// One where an environment map's sheet joins the albedo the light falls on, as the
        /// rasterizer's `preLightEnv` adds it before its lighting multiplies — `[Shaders] apply
        /// lighting to environment maps` — and nought where it is light of its own past the
        /// lighting, which is that setting's default.
        uint mLitEnvironmentMaps;

        /// One where the eye meets a soft edge's texels under the cut by their alpha (`cutAt`), and
        /// nought where it cuts them as every other ray does: `Rtx::Reconstruction::mSoftEdgeDither`.
        uint mSoftEdgeDither;

        /// One where the trace stores the nearest layer's surface and motion (`CHANNEL_PANE_SURFACE`,
        /// `CHANNEL_PANE_MOTION`), which the pane filter alone reads, and nought where the frame's light is
        /// filtered by the upscaler: the chain keeps those channels a texel wide there (`ChannelKeeping`).
        /// A branch every lane of a dispatch takes alike, on two stores.
        uint mPaneTracks;

        /// One where the lamp lists the frame before wrote stand for this frame's tiles
        /// (`GpuTables::mLampLists`): the chain's channels wrote them at this extent, and the previous
        /// basis is one a reprojection finds a tile through. Nought where there is no frame before, or
        /// another camera's, which steers no draw.
        uint mLampListsHeld;

        /// The share of a pixel's light under which a source is never drawn for its shadow bit —
        /// `SHADOW_DRAW_FLOOR` unless a run names another, `Rtx::Reconstruction::mShadowFloor`.
        float mShadowFloor;

        /// How many lamp candidates a shading point that composes its light draws from its cell —
        /// `LAMP_CANDIDATES` unless a run names another, `Rtx::Reconstruction::mLampCandidates`.
        uint mLampCandidates;

        /// How many bounce rays the eye's surface traces, nought where `[RTX] indirect lighting` is off and otherwise
        /// up to `MAX_BOUNCE_RAYS` — `Rtx::SamplingSwitches::mBounceRays`: the player's `[RTX] bounce rays` in the
        /// world's frame, and one in a picture's and a frame built by hand. The bounce's and the landing's stages run
        /// once a ray, each worth one over their count (`startBounce`).
        uint mBounceRays;

        /// Where inside its pixel the previous frame sampled — its world eye's jitter — or this
        /// frame's where it has none: the one source of it for every history and for the
        /// upscaler, `Rtx::sampleFrame`.
        vec2 mPreviousJitter;

        /// Where every table a hit reads is. `GpuTables` says why it rides here. **Last, because it is
        /// eight-aligned and nothing before it is**: anywhere else it would pad the middle of a struct two
        /// languages have to agree on. Everything above it is four-aligned and sums to a multiple of
        /// eight, so no byte is an implicit pad, which no copy carries and no comparison reads; a
        /// four-byte field added above it comes with a named pad (`uint mPadding RTX_ZERO;`), which the
        /// assert that `mTables` follows `mPreviousJitter` asks for.
        GpuTables mTables;
    };

    /// What the puffs' composite is pushed beside the frame block: the frame's eyes on the grid of the
    /// picture it composites over (the output's past an upscaler, the traced one where nothing
    /// upscales, a picture's own inside a larger image), whose extent is that grid's. Pushed, because
    /// the launch runs over the traced grid and says only that extent (`spritecomposite.rgen`).
    /// **Made on the host by `cameraOnGrid`**, as the display pass's camera is, so the two draw one
    /// ray through a shown pixel.
    struct PuffConstants
    {
        Eyes mShown;
    };

#ifdef RTX_HOST
    /// The sun as the frame carries it, with the one limb every sun is drawn from (`skySource`).
    inline SkySource sunSource(const vec3& direction, const vec3& irradiance)
    {
        return skySource(direction, irradiance, SUN_SHADOW_RADIUS);
    }

    /// Whether a source in the sky lights anything this frame: the sun, or a moon. A moon that is
    /// drawn and lights nothing — one fading out around dawn — does not count, and neither does a
    /// room, whose sky has no sources. Where none does, no pixel has a sky source's light to shadow.
    inline bool skySourceLights(const VisibilityConstants& frame)
    {
        bool lights = frame.mSun.mIrradiance != vec3();
        for (const MoonDisc& moon : frame.mMoons)
            lights = lights || moon.mSource.mIrradiance != vec3();
        return lights;
    }

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that reads
    // them are different compilers.
    static_assert(offsetof(VisibilityConstants, mTables) == 1464, "GpuTables must land eight-aligned and last");
    static_assert(offsetof(VisibilityConstants, mTables)
            == offsetof(VisibilityConstants, mPreviousJitter) + sizeof(VisibilityConstants::mPreviousJitter),
        "no implicit pad before GpuTables: name one, so a copy carries it and a comparison reads it");
    static_assert(sizeof(VisibilityConstants) == 1680, "VisibilityConstants must be scalar-packed on every side");
    static_assert(sizeof(PuffConstants) == 152, "PuffConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
