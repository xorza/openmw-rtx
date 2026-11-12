#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SCENE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SCENE_H

#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// The scene's tables and the scale its brightnesses are measured on, as both sides see them, in scalar
// block layout. Every number somebody reaches for when the frame looks wrong is in `look.h`, which
// includes this, so a dial and the size it is measured against stay one statement.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// A slot of the bindless texture array that is not one: every optional map (cloud deck, star
    /// sheet, dark, emissive, environment) tests for it before the read. The diffuse is never
    /// optional: a material with none names `TEXTURE_NEUTRAL`, so every material reads one path.
    const uint NO_TEXTURE = 0xFFFFFFFFu;

    /// The bit over a slot's texel count (`GpuTables::mTextureTexels`) that says the slot draws the
    /// backend's stand-in: its file did not read, it is past the side the device takes, or the device
    /// had no room. **A reader of an optional map reads it as `NO_TEXTURE`**, as the stand-in is grey
    /// and grey is a wrong normal, specular, dark or cloud map; only a base colour draws it.
    /// On the device and not in the rows, because the backend decides as textures come and go.
    const uint TEXTURE_STANDS_IN = 0x80000000u;

    /// Where a slot's texel word holds the wrap its file states (`Rtx::TextureWrap`): the two bits
    /// under `TEXTURE_STANDS_IN`, in the word a level's read already loads for `coneLod`.
    const uint TEXTURE_WRAP_SHIFT = 29u;

    /// The texel count under the wrap and `TEXTURE_STANDS_IN`.
    const uint TEXTURE_TEXELS = 0x1FFFFFFFu;

    /// The largest side a texture stands at, whatever the device takes: its square is the largest
    /// count `TEXTURE_TEXELS` holds whole. A texture past it stands from its first level within it.
    const uint TEXTURE_SIDE_MOST = 16384u;

    /// How many slots the bindless array holds. A PBR pack gives a diffuse a normal and a specular
    /// slot beside it (about 3700 at the largest bench place), so twice that. **Not 16384**: the
    /// layers' kernel spilled 480 bytes a lane on a low-end AMD integrated GPU, against 192 at 8192
    /// (`docs/research/2026-10-22-amd-integrated-spill-ceilings.md`).
    const uint TEXTURE_SLOTS = 8192u;

    /// The one slot the scene never hands out: one texel of `UNTEXTURED_ALBEDO` with an alpha of one.
    /// **A texture and not a sentinel**, so an untextured surface takes the textured path with no
    /// per-lane test. **The last slot**, so the scene's own table still hands out from nought.
    const uint TEXTURE_NEUTRAL = TEXTURE_SLOTS - 1u;

/// What every texture this renderer writes is stored as: a chain a file did not carry, a sprite's
/// light bake and a ground composite. Read back through the file's curve where the file had one.
#define TEXTURE_WRITTEN_FORMAT STORAGE_RGBA8

    /// Elements in one block of the shared vertex buffers, and of the index buffer.
    ///
    /// **Blocked so a buffer grows by appending**: one allocation moves when it grows, and every BLAS
    /// holds a device address into it. A shader resolves an id with `id / BLOCK` and `id % BLOCK`.
    /// **Bounded below by the largest run one mesh can ask for**, as a run may not straddle a block;
    /// a 65×65 terrain chunk leaves four orders of headroom. The tail of a block too short for the
    /// next run is handed out again by `Rtx::RunAllocator`. Three megabytes of positions a block.
    const uint VERTEX_BLOCK = 256u * 1024u;

    /// The block a deforming mesh's normals and tangents start at among the ids a frame slot's tables
    /// resolve: past any block a standing mesh can reach (this many blocks of normals are twelve
    /// gigabytes). A skin poses into the slot's own blocks there; every other vertex's are shared.
    const uint POSED_FIRST_BLOCK = 4096u;

    /// The index buffer wants its own number: a triangle soup has three indices a vertex and a
    /// terrain chunk closer to six.
    const uint INDEX_BLOCK = 1024u * 1024u;

    /// How many world units the game puts in a metre, which is `Constants::UnitsPerMeter`. Here so a
    /// coefficient published per metre (water's absorption) stays in the units it was measured in.
    const float UNITS_PER_METRE = 69.99125f;

    /// The circle constant, and the Lambertian BRDF's reciprocal of it. Shared because a lamp's
    /// intensity is built with the factor that cancels the shader's division by `INV_PI`.
    const float PI = 3.14159265f;
    const float INV_PI = 1.0f / PI;

    /// A whole turn, which is how a wavelength becomes a wavenumber. Twice `PI` to the bit, because
    /// doubling a float is exact.
    const float TAU = 2.0f * PI;

    /// What an isotropic phase function is worth: one over the solid angle of the sphere.
    /// **A light owes this to the air even with no phase function**: a lamp reaches the fog as
    /// irradiance and what returns to the eye is that spread over every direction. Left out, lamps
    /// light the air twelve and a half times too strongly.
    const float INV_FOUR_PI = 0.25f * INV_PI;

    /// How many independent numbers one pixel draws in one frame: a channel of the blue-noise tile
    /// apiece, so two draws are uncorrelated with each other as well as with the neighbours'. Shared
    /// because the tile is generated on the host with exactly this many masks. **Exactly the number
    /// drawn, not a round one**: a spare channel would have no honest step to advance by.
    const uint RANDOM_STREAMS = 12;

    /// Which channel of the tile each draw takes; a pair costs two, hence the gaps after the column
    /// and the bounce. **A channel apiece, not a salt on a shared one**: a fog offset and a bounce
    /// elevation drawn from one number correlate. **The column's two offsets are a pair**: salted
    /// apart by the pixel alone, both turned by the same step and every column walked one diagonal.
    const uint STREAM_FOG_COLUMN = 0u;
    const uint STREAM_BOUNCE = 2u;

    /// Where the water's shaft march starts inside its first step. **Its own channel and not the
    /// fog's**: a pixel whose air started late would have its water start late, and the two marches
    /// lie end to end along one line.
    const uint STREAM_WATER = 4u;

    /// Where a froxel's sample sits inside its own slice, down the column's ray: a march offset like
    /// the water's, and its own channel for the same reason.
    const uint STREAM_FOG_ALONG = 5u;

    /// Where on the sky's disc and on the held lamp's sphere the eye's split hit aims its two shadow
    /// rays, and which sky source it draws (`GatherRun`). **Blue, because the shadow denoiser filters
    /// these rays' bits** (Heitz and Belcour 2019; NRD); deeper paths keep the hash, which a replay
    /// of their far end needs.
    const uint STREAM_SUN_DISC = 6u;
    const uint STREAM_LAMP_DISC = 8u;
    const uint STREAM_SKY_PICK = 10u;

    /// The tone pass's dither under the eight-bit store (`tone.comp`): blue, so the error a byte
    /// leaves is spread where the eye sees least of it, and its own channel, so it follows none of
    /// the trace's draws at the same pixel.
    const uint STREAM_DITHER = 11u;

    /// What `VisibilityConstants::mNoise` says the per-pixel draws come from: the tile, turned
    /// by an irrational step each frame, or a hashed counter seeded by the pixel, the frame and
    /// the stream. `Rtx::NoiseSource` is the host's spelling and `randomAt` the reader.
    const uint NOISE_BLUE_TILE = 0u;
    const uint NOISE_WHITE_HASH = 1u;

    /// Edge of the blue-noise tile, in pixels: small enough that generating it is quick, large enough
    /// that the repeat does not read. The per-frame turn moves values and not where the tile repeats;
    /// what hides the repeat is the pattern having no low frequencies.
    const uint BLUE_NOISE_EXTENT = 64;

    /// How many texels along each side of the fog's baked volume, how many cells of noise it holds
    /// across, and how many levels sit under it.
    ///
    /// **A volume, because a flat field makes columns**: with no third axis a bank runs from the
    /// ground straight up. **One octave of eight cells**: stacked octaves over few cells are a
    /// lattice of straight planes; the fractal look comes from the three scales `fogShape` reads it
    /// at, whose beat at `FOG_LACUNARITY` hides the tile. Two channels and a chain are 73 kilobytes.
    const uint FOG_FIELD_SIZE = 32u;
    const uint FOG_FIELD_CELLS = 8u;
    const uint FOG_FIELD_LEVELS = 6u;

    /// The standard deviation every level of that field is normalised to. **A property of the field
    /// and not of the level**, so the coverage band is one pair of numbers: a level is a mean of
    /// eight texels and its own spread narrows going up. `FOG_COVERAGE` was set against this figure.
    const float FOG_FIELD_SPREAD = 0.1204f;

    /// How many scales that one tile is read at. **One tile repeats and three do not**: read at
    /// scales that are not whole fractions of each other they never come back into step, and what
    /// shows is the beat. Three reaches thirty-six units at the fine end, finer than any near step.
    const uint FOG_SCALES = 3u;

    /// How many pixels of the frame one column of the fog volume stands for, on each axis. The
    /// field's finest scale (`FOG_GRAIN`) covers far more than a column, so a smaller number buys a
    /// sharper copy of detail that is not there, at a cost in all three axes.
    ///
    /// **Twelve and not eight** (RTX Remix runs sixteen): on the default suite `air` fell 40 to 44%,
    /// `column` 27 to 36%, the frame 0.13 to 0.35 ms at the median, the volume by 91 MB at 1707×960,
    /// for a halo and a hill's edge moving up to 30 levels on 0.15% of pixels.
    const uint FOG_VOLUME_SCALE = 12u;

    /// How many slices a column is integrated in: more than the march it replaces spends over one
    /// ray, since a column stands for `FOG_VOLUME_SCALE` squared pixels and pays once for all of them.
    const uint FOG_VOLUME_SLICES = 64u;

    /// How far under its nominal level the sea's own surface is placed, in world units.
    ///
    /// **Coplanar surfaces have no intersection order, so one is imposed.** Terrain heights are
    /// multiples of eight and the sea sits at zero, so ground at sea level is exactly in the water
    /// plane and each ray rounds toward either one: a coastal flat comes back as salt and pepper.
    /// The rasterizer's blend hides the tie; a ray's first hit has nothing to blend into, so it is
    /// broken once, in the geometry, in the ground's favour (a flat at sea level is a shore).
    /// **Bounded at both ends**: above the intersection's rounding (a float's last place is 0.04 some
    /// 330,000 units out) and far under what the eye reads (this is seven millimetres).
    const float WATER_TIE_BREAK = 0.5f;

    /// A ceiling on how bright a focus is allowed to get. Where the refracted bundle collapses to a
    /// line the Jacobian goes to zero and the intensity to infinity (a caustic cusp), which no
    /// exposure could hold. **It sets how bright the lines are, not whether there are any**
    /// (`WATER_CAUSTIC_FOLD` does). **Here, not in `look.h`, because `causticGain` is fitted against
    /// it** and the test of the fit has to read both.
    const float WATER_CAUSTIC_MAX = 2.0f;

    /// What the fit below is made of. The numerator's coefficient is the denominator's plus one,
    /// which makes the curve's second order exactly `1 + f^2`; loose numbers would hide that.
    const float WATER_CAUSTIC_GAIN_SQUARE = 1.1017f;
    const float WATER_CAUSTIC_GAIN_CUBE = 0.1872f;
    const float WATER_CAUSTIC_GAIN_QUARTIC = 0.10896f;

    /// The mean of `1 / max(|det(I - b H)|, 1 / WATER_CAUSTIC_MAX)` over a sea of this fold: what the
    /// caustic is divided by to move light rather than make it.
    ///
    /// **Light density at `q = p - b grad(h)` is the reciprocal of the Jacobian, weighted by bed
    /// area.** Reading it evenly over the surface is a mean of a reciprocal where the reciprocal of a
    /// mean was wanted, and comes out high. **A curve and not a series**: the second order is
    /// `1 + f^2`, but `WATER_CAUSTIC_FOLD` of three puts `f` at an rms of three, where an expansion
    /// describes nothing. **It is a hump**: the mean climbs to 1.286 near one, then the ceiling cuts
    /// cusps faster than the excess accumulates and it falls through one at `f = 2.28`.
    /// Fitted to a Monte Carlo over an isotropic Gaussian field's Hessian (`Var[Hxx] = Var[Hyy] = 3c`,
    /// `Var[Hxy] = Cov[Hxx, Hyy] = c`, so `f` decides all): within 0.015 of four million draws up to
    /// fold 4.5, as `RtxCausticGainTest` says.
    /// @param fold `b` times the root of the curvature variance the cone can still resolve, which is
    ///        how far toward its own first fold the map has been run.
    RTX_SHADER float causticGain(float fold)
    {
        const float squared = fold * fold;

        return (1.0f + (WATER_CAUSTIC_GAIN_SQUARE + 1.0f) * squared)
            / (1.0f + WATER_CAUSTIC_GAIN_SQUARE * squared + WATER_CAUSTIC_GAIN_CUBE * squared * fold
                + WATER_CAUSTIC_GAIN_QUARTIC * squared * squared);
    }

    /// Which instances a ray is interested in: the instance mask, one class bit per instance or
    /// `MASK_MEDIUM` instead of it, tested against the camera's `VisibilityConstants::mRayMask`.
    /// This is the camera's cull mask as a ray tracer reads it: a placement carries the class of the
    /// innermost node on its path (`Rtx::InstanceClass`); `MWRender::describeView` maps one to the other.
    /// **Water must not cast a shadow, and the mask tells traversal so at no cost**: building water
    /// non-opaque costs half the frame rate, as every shadow ray over the sea then invokes a shader.
    const uint MASK_STATIC = 0x01u;
    const uint MASK_WATER = 0x02u;

    /// The player's own arms in first person: seen by the eye and by no other ray. **A pair of hands
    /// with no body behind them would cast a shadow of a pair of hands**, which the game never showed
    /// (`Mask_FirstPerson` is in no shadow-casting mask nor the reflection camera's).
    const uint MASK_FIRST_PERSON = 0x04u;

    /// An actor or the player (`Mask_Actor | Mask_Player`) and an effect hung on something
    /// (`Mask_Effect`). **One bit for actor and player**: no camera draws one without the other and
    /// bits are nearly spent.
    const uint MASK_ACTOR = 0x10u;
    const uint MASK_EFFECT = 0x20u;

    /// A camera bit and never an instance's: whether this trace bins and draws the sprites. The
    /// rasterizer's `Mask_ParticleSystem`, which a map tile's camera leaves out.
    const uint MASK_PARTICLE = 0x40u;

    /// A surface that adds to the frame and covers nothing (`Rtx::Material::isAdditive`): a magic
    /// effect's sheet, the Heart of Lorkhan's. **The last bit, and alone on its instance**: no cast
    /// but `additiveAlong` carries it, so the eye, shadows, bounces and the medium walk never meet one.
    const uint MASK_ADDITIVE = 0x80u;

    /// Every class the eye can ask for. A trace with no camera of its own — the harness's, the
    /// tests' — asks for all of them.
    const uint MASK_EVERY_CLASS
        = MASK_STATIC | MASK_WATER | MASK_FIRST_PERSON | MASK_ACTOR | MASK_EFFECT | MASK_PARTICLE;

    /// What every ray but the eye's own casts with: what the camera draws, less the arms
    /// (`MASK_FIRST_PERSON`), less the water (`MASK_WATER`) which shadows nothing, less the
    /// particles, which are sprites and not instances.
    RTX_SHADER uint solidMask(uint rayMask)
    {
        return rayMask & ~(MASK_FIRST_PERSON | MASK_WATER | MASK_PARTICLE);
    }

    /// What the world's own eye ray casts with: the camera's mask less the arms, which are the arms'
    /// eye's alone (`Eyes::mArms`), traced first under a projection of their own.
    RTX_SHADER uint worldMask(uint rayMask)
    {
        return rayMask & ~MASK_FIRST_PERSON;
    }

    /// What shelters a falling sprite: statics and objects, nothing that moves. The rasterizer's
    /// `PrecipitationOccluder` culls with `Mask_Object | Mask_Static`, so an actor's hat keeps no rain off.
    RTX_SHADER uint shelterMask(uint rayMask)
    {
        return rayMask & MASK_STATIC;
    }

    /// How many see-through surfaces the eye peels off before it draws what is under them.
    ///
    /// **A person is a stack and a window is not**: a cuirass over a skirt over a leg is three
    /// surfaces, so a faded actor peeled one layer deep shows its nearest layer faded and every layer
    /// under it at full strength. **Four, a dressed person and what is behind them**; the surface
    /// after the last is drawn as the solid it stands in for. Media are not peeled (`MASK_MEDIUM`).
    /// Each layer costs a traversal only on the pixels that reach it.
    const uint PEEL_LAYERS = 4u;

    /// A surface that is nowhere opaque, gathered as a depth along the ray rather than met.
    /// **Carried instead of the class bit**: only the shadow and ambient rays (`passageAsk`) and
    /// `mediumAlong` want a medium, and the eye, a bounce and a water ray otherwise walked past every
    /// shell after a cutout test and a row load apiece. `GpuInstance::mClass` answers the class test.
    const uint MASK_MEDIUM = 0x08u;

    /// The placement is the model of a light that gives light (`Rtx::MeshInstance::mLampBody`), so a
    /// diffuse bounce takes no glow from it (`landingLightOf`): its lamp delivers that light.
    /// **In `GpuInstance::mClass` above the class bits**; the structure's own mask
    /// (`Rtx::InstanceRecord::mMask`) never carries it, so no traversal sees it.
    const uint INSTANCE_LAMP_BODY = 0x100u;

    /// The material is a medium — `Rtx::Material::isMedium`. **A bit and not a second float**, as the
    /// row is read at every candidate the eye walks past; density is not stored (the texture's alpha
    /// is what a crossing square to the surface hides, and obliquity is all a thickness adds).
    const uint MATERIAL_MEDIUM = 0x01u;

    /// The mesh's per-vertex colour replaces this material's diffuse tint (`Rtx::VertexColour::Tint`
    /// and `Diffuse`: every piece of ground and over half the models). **A bit and not a second
    /// colour on the row**: a mesh with no colour holds white, so the shader's `mix` against nought or
    /// one costs no branch and no extra load.
    const uint MATERIAL_VERTEX_TINT = 0x02u;

    /// The same colour replaces this material's ambient colour — `Rtx::VertexColour::Tint` and
    /// `Ambient`. Above the alpha test's sides, which have the bits under it.
    const uint MATERIAL_VERTEX_AMBIENT = 0x80000u;

    /// No lamp lights the surface — `Rtx::Material::mLampLit` false, which is groundcover where
    /// `[Groundcover] point lighting` is off. The sun, the sky and the bounce light it as before.
    const uint MATERIAL_NO_LAMPS = 0x100000u;

    /// The surface is a blended mask — `Rtx::Material::isSoftEdged`: a blend over what is behind it,
    /// with no test of its own, whose texture reaches solid. The eye meets a texel its
    /// `mAlphaReference` would drop with a chance of its alpha (`cutAt`), so the frames average to
    /// the blend `objects.frag` draws, and every other ray keeps the cut.
    const uint MATERIAL_SOFT_EDGE = 0x200000u;

    /// What a pane is cut at, and the least a soft edge's dither cuts at: half a step of an 8-bit alpha.
    /// **A blend draws a texel of alpha nought as nothing**, so such a texel is a hole to every ray,
    /// exactly; a filtered sample below half a step covers less than the display resolves.
    const float ALPHA_PANE_CUTOFF = 0.5f / 255.0f;

    /// The same colour replaces this material's glow instead — `Rtx::VertexColour::Glow`. The
    /// light mode that goes with it already took the diffuse and the ambient to nought, so such a
    /// surface is its glow and nothing else.
    const uint MATERIAL_VERTEX_GLOW = 0x04u;

    /// The surface adds whole, `ONE, ONE`: its alpha unread. That it adds at all is said by the
    /// mask its placements carry, `MASK_ADDITIVE` alone, which is what brings it to
    /// `additiveAlong` and to nothing that shades a hit — so no bit here says so twice.
    const uint MATERIAL_ADD_WHOLE = 0x08u;

    /// Ground with no composite: the albedo is the sum over `mLayerOffset`'s run and the diffuse is
    /// the neutral slot. A multi-layer chunk names its composite as the diffuse once baked and drops
    /// this bit (`CellPlacer::flatteningOf` asks for one).
    const uint MATERIAL_STACKED = 0x10u;

    /// Ground whose composite stands in only where no picture is drawn from the hit (a bounce's far
    /// end) and whose eye, reflections and bed sum the stack: a chunk inside the active grid,
    /// `Rtx::GroundFlattening::Undetailed`.
    const uint MATERIAL_STACKED_SEEN = 0x400000u;

    /// A blended material whose dark map's alpha falls below solid (`Rtx::Material::mDarkBelowSolid`):
    /// a see-through hit's coverage takes the dark map's alpha too, where a scene that places one
    /// compiles the read in (`HAS_DARK_ALPHA`).
    const uint MATERIAL_DARK_ALPHA = 0x800000u;

    /// The normal map's alpha is a height the texture coordinates are shifted by —
    /// `Rtx::Material::mParallax`, and `parallaxShift` says by how much.
    const uint MATERIAL_PARALLAX = 0x20u;

    /// The surface is a pane — `Rtx::Material::isTranslucent`: what is behind it shows through by its
    /// texture's alpha times `mOpacity`, and it has no mask. **A bit and not `mOpacity` below one**:
    /// a lantern's glass has an opacity of one and its texture says how much glass.
    const uint MATERIAL_TRANSLUCENT = 0x40u;

    /// Which texture unit the dark map and the glow map are bound at, each in these bits of
    /// `mFlags` — `GpuMesh::mUnitStreams` says which stream that unit reads.
    const uint MATERIAL_DARK_UNIT_SHIFT = 8u;
    const uint MATERIAL_EMISSIVE_UNIT_SHIFT = 12u;
    const uint MATERIAL_UNIT_MASK = 0x0Fu;

    /// Which sides of the alpha test's reference a texel passes on, one bit each: OpenGL's comparison
    /// functions are `GL_NEVER` plus a set of these (`GL_LESS` below, `GL_GEQUAL` at and above). In
    /// these bits of `mFlags`, all three where the surface has no mask.
    const uint ALPHA_PASSES_BELOW = 0x1u;
    const uint ALPHA_PASSES_AT = 0x2u;
    const uint ALPHA_PASSES_ABOVE = 0x4u;
    const uint ALPHA_PASSES_ALL = 0x7u;
    const uint MATERIAL_ALPHA_PASSES_SHIFT = 16u;

    /// Whether `alpha` passes a test at `reference` that passes on the sides `passes` names: the
    /// side selected, and no branch on the function.
    RTX_SHADER bool alphaPasses(uint passes, float alpha, float reference)
    {
        const uint side = alpha < reference ? ALPHA_PASSES_BELOW
            : alpha == reference            ? ALPHA_PASSES_AT
                                            : ALPHA_PASSES_ABOVE;
        return (passes & side) != 0u;
    }

    /// Whether the test fails some alpha from nought to one: whether it cuts anything at all. A
    /// test at least nought passes everything, and one above nought cuts every bare texel.
    RTX_SHADER bool alphaTestCuts(uint passes, float reference)
    {
        const bool below = (passes & ALPHA_PASSES_BELOW) == 0u && reference > 0.0f;
        const bool at = (passes & ALPHA_PASSES_AT) == 0u && reference >= 0.0f && reference <= 1.0f;
        const bool above = (passes & ALPHA_PASSES_ABOVE) == 0u && reference < 1.0f;
        return below || at || above;
    }

    /// A sprite emitter that adds — `Rtx::BlendKind::Add` or `AddWhole` — and one whose sprites
    /// fall from the sky: `spriteshelter.rgen` drops those that stand under cover.
    const uint EMITTER_ADDITIVE = 0x01u;
    const uint EMITTER_FALLS = 0x02u;

    /// An emitter that adds whole, `ONE, ONE`: its texels add their colour with alpha unread, as
    /// `MATERIAL_ADD_WHOLE` says of a surface. The resolver settles its sprites' own alpha at one.
    const uint EMITTER_ADD_WHOLE = 0x04u;

    /// An emitter on the arms, whose sprites are billboards and not balls: `ballCrossing` says why.
    const uint EMITTER_FIRST_PERSON = 0x08u;

    /// The content doubled every triangle of this mesh for its back — `Rtx::FoldedShape::mSheet`.
    /// With a mask on its material that is a leaf, and `SHEET_TRANSMISSION` says what the light on
    /// its far side is worth to it.
    const uint MESH_SHEET = 0x01u;

    /// The mesh carries tangents — `Rtx::MeshRange::mTangents` — so a hit on it has a frame to read
    /// a normal map through. **What a traversal asks before it fetches three tangent words**, on the
    /// mesh row it already holds: a vanilla scene has none, and every hit in it skips the fetch.
    const uint MESH_TANGENTS = 0x02u;

    /// Where a mesh's vertices and indices begin in the shared buffers.
    ///
    /// Indices are mesh-local, so a triangle's vertex is `mVertexOffset` plus what the index says.
    struct GpuMesh
    {
        uint mVertexOffset;
        uint mIndexOffset;

        /// What the shape pass found this mesh to be — `MESH_SHEET` and `MESH_TANGENTS`. **Bits, not
        /// two words**: every ray that lands fetches this row.
        uint mShape;

        /// Where this mesh's second set of texture coordinates begins in the blocks of their own, or
        /// `NO_RUN` for a mesh that brought none (nearly every mesh). Mesh-local, as `mVertexOffset` is.
        uint mSecondTexCoordOffset;

        /// One bit per texture unit, set where that unit reads the second set
        /// (`Rtx::MeshArrays::mUnitStreams`): a material is shared across geometries and the binding
        /// is each geometry's own.
        uint mUnitStreams;

        /// Where this mesh's posed vertices sit among the deforming meshes' (`Rtx::MeshRange::
        /// mBindOffset`), or `NO_RUN` for a mesh that stands: what lets a hit on a body read where its
        /// triangle stood last frame.
        uint mBindOffset;

        /// What a vertex id is moved by to name its normal and its tangent: nought for a mesh that
        /// stands, and for one that deforms, modulo 2³², from `mVertexOffset` to its bind run past
        /// `POSED_FIRST_BLOCK`. An add and not a select of the table: the select cost 1.2 % of the
        /// trace at `seyda-neen-ship`.
        uint mNormalShift;
    };

    /// A run a mesh does not have: `GpuMesh::mSecondTexCoordOffset` for a mesh that brought no
    /// second set of texture coordinates, and `GpuMesh::mBindOffset` for one that does not deform.
    const uint NO_RUN = 0xFFFFFFFFu;

    /// A vertex's tangent as one word: the direction folded onto the octahedron, each coordinate
    /// stepped to `2 * TANGENT_STEPS + 1` values in fifteen bits, the bitangent's handedness, and
    /// whether there is a tangent at all (nought is none). `Rtx::packTangent` writes it and
    /// `unpackTangent` reads it. **An odd count of steps**, so nought, one and minus one are exact
    /// and an axis-aligned tangent comes back as the axis it was.
    const uint TANGENT_PRESENT = 0x80000000u;
    const uint TANGENT_FLIPPED = 0x40000000u;
    const uint TANGENT_COORDINATE_BITS = 15u;
    const uint TANGENT_COORDINATE_MASK = (1u << TANGENT_COORDINATE_BITS) - 1u;
    const uint TANGENT_STEPS = TANGENT_COORDINATE_MASK / 2u;
    const float TANGENT_STEP = 1.0f / float(TANGENT_STEPS);

    /// A vertex's colour in linear light: red and green as two half floats in one word, low half
    /// first as `packHalf2x16` lays them, and blue in the low half of the other. **Halves**: a third
    /// of the bytes every hit reads, and unlike normalized words they hold a content file's colour
    /// past one, to eleven bits. `Rtx::MeshTable` packs it (`Rtx::toHalf`).
    struct GpuVertexColour
    {
        uint mRedGreen;
        uint mBlue;
    };

    /// The device's material table puts the untextured material at row nought for the table's life
    /// and the scene's material `i` at row `i + 1`. **First and not past the end**, so a growing
    /// table appends and moves no row an instance names: a sentinel at the end handed its old row to
    /// the next material.
    const uint MATERIAL_ROW_UNTEXTURED = 0u;
    const uint MATERIAL_ROW_FIRST = 1u;

    struct GpuInstance
    {
        uint mMesh;

        /// A row of the material table — `MATERIAL_ROW_FIRST` says how a material's index maps to it.
        uint mMaterial;

        /// How much of this placement is there, before its material and texture are asked: one for
        /// everything the game is not hiding. See `Rtx::MeshInstance::mOpacity` for why a fade belongs
        /// to a placement and not to a material.
        float mOpacity;

        /// The class bits this placement answers to (`Rtx::InstanceRecord::mClass`), and
        /// `INSTANCE_LAMP_BODY` above them. A medium's structure mask is `MASK_MEDIUM` alone, so the one
        /// ray that sums a medium asks this word instead (`candidateStops`).
        uint mClass;

        /// World space to where this instance was on the previous frame, as three rows of four: the
        /// identity for anything that did not move, so a static surface has a motion vector of exactly
        /// zero rather than rounding. See `Rtx::InstanceRecord::mMotion`.
        vec4 mMotion[3];

        /// The mesh's own space to the world, the top level's row, and back, as three rows of four
        /// each (`Rtx::InstanceRecord::mInverse`): what a hit is resolved through, here and not off
        /// the query, so a stage that resolves a hit the trace stored resolves the trace's surface
        /// (`committedHit`).
        vec4 mToWorld[3];
        vec4 mToObject[3];
    };

    /// `GpuLight::mTraits`' fill bit, and where the class bits start above it.
    const uint LIGHT_FILL = 1u;
    const uint LIGHT_CLASS_SHIFT = 8u;

    /// One light placed in the world — a lamp, or the fill a magic effect glows with — with
    /// everything a shader needs already derived: a `LIGH` record has a colour and a radius and no
    /// intensity, and `Rtx::makeLight` settles both on the way in. **The scene's own row, and the
    /// device's**: `Rtx::Light` is this struct, hashed whole by the digest and uploaded as it lies.
    struct GpuLight
    {
        vec3 mPosition;

        /// Radiant intensity, linear, colour folded in, scaled by the square of the recorded radius
        /// (so a lantern and a candle differ by size). **Negative for a lamp that takes light away**,
        /// a `Negative` record: the grid lists it under a key of its own (`Rtx::LightGrid`) and it
        /// darkens a surface's direct lamp term by its unshadowed share, floored at nought, as the
        /// rasterizer does: no shadow, no bounce, nothing in the air.
        vec3 mIntensity RTX_ZERO;

        /// How far the light reaches, beyond which it contributes exactly nothing. Stretched from the
        /// recorded radius (64 to 256 units under an ambient filling the room), as here the lamps have
        /// to light the place.
        float mReach RTX_ZERO;

        /// How big the glowing part is, in world units: the flame, which a shadow ray opens to for a
        /// penumbra as wide as it is. Zero, which a hand-built light carries, is a point. **It makes
        /// the falloff a sphere's rather than a point's**: an inverse square runs away where the air
        /// beside a lamp is sampled, and a source with an extent flattens inside its own surface.
        float mSourceRadius RTX_ZERO;

        /// How far short of the centre a shadow ray stops: nought for a record's lamp, whose ray
        /// runs to the flame (`Rtx::makeLight` says why), and a fill's whole ball, inside which
        /// nothing casts a shadow (`Rtx::Glow::makeLight`).
        float mClearance RTX_ZERO;

        /// Whether this light is a fill, and the classes it answers to, as `lightTraits` packs them:
        /// one word, not a bool, because the record is hashed whole and a bool leaves bytes nothing
        /// wrote. **A fill** is a lamp whose flame is a ball `mSourceRadius` wide, lit from every side
        /// inside it (`Rtx::Glow::makeLight`). **A lamp lights only a view that shows its class**
        /// (`lightShown`): the rasterizer collects no light from a node its cull does not reach.
        uint mTraits RTX_INIT(MASK_EVERY_CLASS << LIGHT_CLASS_SHIFT);
    };

    /// A light's traits: whether it is a fill, and the class bits of the placement it hangs under.
    RTX_SHADER uint lightTraits(bool fill, uint classes)
    {
        return (fill ? LIGHT_FILL : 0u) | (classes << LIGHT_CLASS_SHIFT);
    }

    /// One for a fill and nought for a lamp: a factor, so a lamp's arithmetic stays a lamp's.
    RTX_SHADER float lightFill(uint traits)
    {
        return float(traits & LIGHT_FILL);
    }

    /// One where a view with the ray mask `rayMask` shows a class the light answers to, and nought
    /// where it hides every one: a factor on the light's intensity, so a hidden lamp weighs nothing
    /// and no walk branches on it.
    RTX_SHADER float lightShown(uint traits, uint rayMask)
    {
        return ((traits >> LIGHT_CLASS_SHIFT) & rayMask) != 0u ? 1.0f : 0.0f;
    }

    /// Where the lamps were binned, so a shader can find the few that reach a point. **Carried in the
    /// frame's block** (`VisibilityConstants::mLightGrid`): a set of its own would cost a descriptor,
    /// a buffer per frame in flight and a storage read at every lamp lookup. A position outside the
    /// grid reaches no lamp, so its cell is empty by construction rather than by clamping.
    struct GpuLightGrid
    {
        vec3 mOrigin;
        float mInverseCell;
        uvec3 mSize;
    };

    /// One cell of the light grid: its two runs in the light list, the lamps that light it and then
    /// those that take light away (`Rtx::LightGrid::getCells`). One 16-byte row, so both runs lie in
    /// one 32-byte sector. The table claims four-row alignment (`TABLE_ALIGN_ROWS`): sixteen, which
    /// lets one load take the row, traced no faster (`./omw bench`).
    struct GpuLightCell
    {
        uvec2 mCount;
        uvec2 mFirst;
    };

    /// The flat index of the grid's cell `x`, `y`, `z` in a grid `width` by `height` across: the
    /// one arithmetic the host's binning and the shader's lookup have to agree on. Scalars, because
    /// a vector's members are spelled apart in the two languages.
    RTX_SHADER uint lightGridCell(uint x, uint y, uint z, uint width, uint height)
    {
        return (z * height + y) * width + x;
    }
    /// Where every table a hit reads is, as one address apiece. **In the frame block rather than a
    /// descriptor each**: that is a storage-buffer binding per table pushed twice a frame, and a binding
    /// the pass forgets reads whatever the slot holds. An address is a 64-bit integer, so the struct
    /// belongs to the scene; the Vulkan shader makes a `buffer_reference` from each.
    /// **Filled by the pass and not by `describeWorld`**, as `mWaveExtent` and `mLightGrid` are: where a
    /// table lives is known to what placed it, and slot-alternating tables move every frame.
    /// **No size beside an address**: robust access bounds a descriptor read, a pointer has none, so what
    /// stops a read past a table is its count. It costs a load path, a few per cent of an interior's trace
    /// and nothing on exteriors, for the bindings and a class of mistake gone.
    struct GpuTables
    {
        /// The tables of block addresses a global vertex or index id is resolved through. The normals
        /// and tangents are this slot's table, which a deforming mesh's ids reach past
        /// `POSED_FIRST_BLOCK` (`GpuMesh::mNormalShift`).
        uint64 mNormalBlocks;
        uint64 mTangentBlocks;
        uint64 mTexCoordBlocks;
        uint64 mColourBlocks;
        uint64 mIndexBlocks;

        /// The static meshes' positions, which a committed triangle's corners are read from
        /// (`committedHit`); a deforming mesh's are its pose's, `mPoseBlocks`.
        uint64 mPositionBlocks;

        /// The second texture coordinates, in blocks of their own that only meshes carrying a second
        /// set take from (`GpuMesh::mSecondTexCoordOffset`).
        uint64 mSecondTexCoordBlocks;

        uint64 mMeshes;
        uint64 mInstances;
        uint64 mMaterials;
        uint64 mLayers;
        uint64 mMasks;
        uint64 mLights;

        /// The light grid: a `GpuLightCell` a cell, and the list their runs stand in.
        uint64 mLightCells;
        uint64 mLightList;

        uint64 mBlueNoise;

        /// The GGX lobe's two integrals over the cosine to the eye and the roughness
        /// (`Rtx::SpecularAlbedo`), made once and read for every glossy surface.
        uint64 mSpecularAlbedo;

        uint64 mSprites;
        uint64 mEmitters;

        /// One `GpuEmitterFrame` a row of `mEmitters`, the trace's own like the sprites.
        uint64 mEmitterFrames;

        /// The sprite tiles' list, the shape `spritelist.glsl` reads.
        uint64 mSpriteTileList;

        /// One word a screen tile of the same grid: the `PRESENCE_` kinds of instance a ray through
        /// the tile can meet, which `SpriteBin` ORs in from `GpuPresence`'s spheres.
        uint64 mSpritePresence;

        /// The pose blocks, this copy's and the other's: every deforming mesh's vertices as this frame
        /// traces them and as the previous frame did (`GpuMesh::mBindOffset` plus the vertex's index).
        /// The copy this frame does not trace holds the pose as of the frame before, and for a mesh
        /// that did not move the same numbers, so its delta is exactly nought. `reproject.glsl` reads
        /// the pair in object space, where a difference of two positions is exact.
        uint64 mPoseBlocks;
        uint64 mPreviousPoseBlocks;

        /// One `uint` per slot of the bindless array: how many texels its texture holds, the one term
        /// of a mip level that is the texture's own (`coneLod`), as a load where `textureSize` was a
        /// header read on every sample. The backend's texture array writes it a slot at a time, with
        /// the wrap over it (`TEXTURE_WRAP_SHIFT`) and `TEXTURE_STANDS_IN` over both.
        uint64 mTextureTexels;
    };

    /// A ground layer whose diffuse is an authored albedo with the perceptual roughness in its alpha
    /// — a `_diffusespec`, as Wareya's shaders read it (`Rtx::layerEncodingOf`). **Not delit, and a
    /// dielectric**: painted as a surface and not as a picture of one lit, so its F0 is `DIELECTRIC_F0`.
    const uint LAYER_AUTHORED = 0x01u;

    /// A ground layer whose normal map's alpha is a height, as `MATERIAL_PARALLAX` is for a surface:
    /// an `_nh` file the storage found, which `carriesHeight`. The stack shifts by it as the
    /// rasterizer's terrain does; a flattened chunk has no eye to shift toward.
    const uint LAYER_PARALLAX = 0x02u;

    /// One layer of a terrain material: a tiling ground texture and the weights that place it.
    ///
    /// A chunk is four or five of these summed at one hit, where OpenMW draws one alpha-blended pass
    /// per layer. The mask is a grid of weights in the shared mask buffer, not a texture: it is ten
    /// texels across, and sampling it by hand lets the edges clamp instead of repeating.
    /// **The scene's own row, and the device's**: `Rtx::MaterialLayer` is this struct, carried whole
    /// from the land record through `Rtx::PreparedLayer` to the layer run and the bake, so they cannot
    /// disagree about where a texel lands. The texture slot and mask offset are written at adoption.
    struct GpuLayer
    {
        /// The ground texture, which tiles many times across a chunk.
        uint mDiffuse;

        /// Where this layer's weights start in the scene's mask table. The run's count is the grid's
        /// own area, or nought for a layer that covers everything (`Rtx::maskOf`).
        uint mMaskOffset RTX_ZERO;

        /// The grid the weights form. Nought by nought where the layer covers everything.
        uint mMaskWidth RTX_ZERO;
        uint mMaskHeight RTX_ZERO;

        /// Cell texture coordinates to this layer's, as `uv * xy + zw`: the diffuse texture's, and the
        /// mask's. `Rtx::GroundReader` derives both from the tile count as `Terrain::createPasses` does.
        vec4 mDiffuseTransform RTX_ZERO;
        vec4 mMaskTransform RTX_ZERO;

        /// The layer's normal map, read at the diffuse's coordinates, or `NO_TEXTURE`. Its
        /// tangent is the one `terrain.vert` gives every layer: the chunk's x, which is the
        /// world's, with the bitangent `cross(N, x)`.
        uint mNormal RTX_ZERO;

        /// `LAYER_` bits.
        uint mFlags RTX_ZERO;

        /// To the sixty-four bytes std430 would give the row: at fifty-six, every other row's two
        /// `vec4` sit on eight and not sixteen, and every layer a hit sums is read in eight-byte
        /// loads, vanilla ground included.
        uint mPadding[2] RTX_ZERO;

#ifdef RTX_HOST
        /// Two layers are the same when every field is, which is what says a chunk still stands
        /// where a bake of it began.
        bool operator==(const GpuLayer& other) const = default;
#endif
    };

    /// One live particle, drawn as a disc facing the eye. Nothing here reaches an acceleration
    /// structure: the layer is marched against the primary ray and composited in depth order,
    /// without the candidate loop an alpha-blended hit would cost traversal.
    ///
    /// **Composited rather than denoised**, as a rain streak is: coverage arrives as a fraction, so
    /// a sprite finer than a pixel dims instead of flickering. `spritecomposite.rgen` composites it
    /// at the picture's own resolution, after whatever denoises.
    ///
    /// **The scene's own row, and the device's**: `Rtx::Sprite` is this struct, uploaded as it lies.
    /// The two layer counts at the end are the device's alone, and the host leaves them at nought.
    struct GpuSprite
    {
        vec3 mPosition;

        /// Half the sprite's width in world units, which is what `osgParticle` means by a size.
        float mRadius;

        /// The streak's own axis in the world, per unit of `mRadius`, or zero for a sprite that faces
        /// the eye. Per particle, as `MWRender::RainShooter` leans each drop into its wind. Not
        /// normalised, because its length is the shape.
        vec3 mAxis RTX_ZERO;

        /// Linear, and already carrying wherever the particle's own colour ramp has reached.
        vec3 mColour;

        /// What the particle's own fade left of it, multiplied into the texture's alpha at the hit.
        float mAlpha;

        /// Which emitter placed it, written by `Rtx::SceneDesc::addEmitter`. **The walk is over sprites**
        /// and reads the emitter's `GpuEmitterFrame` once where it meets the emitter's run, which a
        /// list of sprites can only keep if a sprite says when its run has changed.
        uint mEmitter RTX_ZERO;

        /// How many sprites of its own emitter stand between this one and the sun, and the sky, each
        /// counted for its fade. `spriteshade.h` counts them once a frame on the device, and
        /// `spritesAlong` thins the light by what one layer of the texture hides.
        float mSunLayers RTX_ZERO;
        float mSkyLayers RTX_ZERO;
    };

    /// How many pixels a side one tile of the sprite list covers.
    ///
    /// **Sixteen**: finer tiles reject more sprites per pixel and cost more to bin; a raindrop is a
    /// few pixels across, so at sixteen it lands in one tile or four. **Eight is a loss**: a drop's
    /// own test is cheap once lamps are walked per emitter, while the fill, which walks every sprite
    /// for every tile, pays for four times the tiles. The tile count is derived from this and the
    /// frame's extent on both sides (`spriteTilesOver`).
    const uint SPRITE_TILE = 16u;

    /// How many tiles cover `pixels` along one axis of the frame. The last one may be part of a tile.
    /// **Derived on every side from `SPRITE_TILE`**, so the trace, the bin and the host reference
    /// cannot disagree about how many tiles there are.
    RTX_SHADER uint spriteTilesOver(uint pixels)
    {
        return (pixels + SPRITE_TILE - 1u) / SPRITE_TILE;
    }

    /// How many tiles a frame of `width` by `height` covers, which is the list's head less one. **The
    /// product in one place**: the scan, the fill, the zeroing fill and the host's sizing each took it,
    /// four chances for the head to be one length here and another there.
    RTX_SHADER uint spriteTilesIn(uint width, uint height)
    {
        return spriteTilesOver(width) * spriteTilesOver(height);
    }

    /// The bit a tile's end entry carries where its run did not fit, beside the frame's sprite count
    /// in the bits under it.
    ///
    /// **Only the tiles past the room are unbinned, and every one before them is whole.** Runs lie in
    /// tile order, so the tiles whose ends fit the capacity are a prefix and are binned as usual;
    /// `spritestarts.comp` writes this into every tile after them, and those alone walk every sprite:
    /// slow and right. A storm that more than doubled within the report's two-frame lag would
    /// otherwise make every pixel walk every sprite. The host grows the buffer and the next frame is
    /// binned whole; `SpriteBin::record` sizes the list so this is rare. A valid end is under
    /// `SpriteListSize::sMostEntries` and never carries the bit.
    const uint SPRITE_TILE_UNBINNED = 0x80000000u;

    /// One particle system: what its sprites are drawn with, and a sphere holding all of them, which
    /// is the whole spatial structure because one rejection throws a small emitter away for almost
    /// every pixel. **The scene's own row, and the device's**: `Rtx::SpriteEmitter` is this struct,
    /// built by `Rtx::SceneDesc::addEmitter`; the run is read through `Rtx::spritesOf`.
    struct GpuEmitter
    {
        vec3 mCentre;

        /// Far enough from `mCentre` to contain every sprite in the run, rim included.
        float mReach;

        /// Where the sprites sit in the scene's sprite table, laid end to end as the emitter
        /// placed them — `Rtx::spritesOf` reads the run back.
        uint mFirst;
        uint mCount;

        /// The sprite texture. Never `NO_TEXTURE` — an emitter without one places no sprites at all,
        /// since a particle's whole silhouette is that texture's alpha.
        uint mTexture;

        /// `EMITTER_ADDITIVE` for a blend that adds (`SRC_ALPHA, ONE`: a flame, which hides nothing
        /// behind it, where the rest blend over and need their colour ramp to fade) and `EMITTER_FALLS`
        /// for what the weather drops, which `spriteshelter.rgen` keeps out from under a roof
        /// (`Rtx::SceneExtractor::extractPrecipitation`).
        uint mFlags;

        /// How wide this emitter's quads are against their own axis, per unit of `GpuSprite::mRadius`
        /// — **or nought, a sprite that faces the eye, nearly everything.** Morrowind's rain is an X
        /// axis squashed to a tenth against a Y axis pointing down. The length and not the direction,
        /// because the march swings the width about the sprite's own axis to meet the ray.
        float mWidth;

        /// The bake of the sprite texture's alpha, or `NO_TEXTURE` for one lit as a flat card.
        /// `Rtx::TextureKind::SpriteLight` says what it holds and `spritesAlong` how it is read.
        uint mLighting;

#ifdef RTX_HOST
        bool isAdditive() const { return (mFlags & EMITTER_ADDITIVE) != 0u; }
        bool falls() const { return (mFlags & EMITTER_FALLS) != 0u; }
#endif
    };

    /// What one trace makes of an emitter, once for every sprite of it: `spriteemitters.rgen` writes a
    /// row an emitter ahead of the trace, and both walks of `spritesAlong` read it where they meet
    /// the emitter's run, instead of working it out at every pixel the emitter covers.
    struct GpuEmitterFrame
    {
        /// The fog's coverage band over the path from the eye to the emitter, taken at the path's
        /// mean-value point: every sprite of the emitter is within `GpuEmitter::mReach` of the same
        /// air, and the band costs a fetch of the fog's tile per scale (`FOG_SCALES`).
        float mBand;

        /// What one layer of the emitter's texture lets through on average, as the base-two
        /// logarithm the walk's two powers share: off the mean of its coarsest level, held under
        /// `SPRITE_ALPHA_LIMIT`.
        float mLayerThrough;

        /// The texture's extent along each of its axes.
        vec2 mTexels;
    };

    /// The kinds of instance the walks along a primary ray look for, and what a tile of
    /// `GpuTables::mSpritePresence` holds: a surface that adds to the frame (`additiveAlong`) and one
    /// the eye passes through, a cloud's shells (`mediumAlong`). **Keeps each walk off pixels it can
    /// find nothing at**: both traverse the top level once a pixel, and one such mesh anywhere in the
    /// loaded cells otherwise had every pixel descend it.
    const uint PRESENCE_ADDITIVE = 1u;
    const uint PRESENCE_MEDIUM = 2u;

    /// And for an instance the bin cannot place by the world camera's tiles: the player's own
    /// arms, which are traced along another camera's rays. Put in every tile.
    const uint PRESENCE_EVERYWHERE = 4u;

    /// Where one instance of those kinds can be met: a sphere about everything it places, in world
    /// space, and the `PRESENCE_` bits it carries. Written by a placement, one row an instance of
    /// either kind, and binned into the screen's tiles by `spriterects.comp` beside the sprites.
    struct GpuPresence
    {
        vec3 mCentre;
        float mRadius;
        uint mKinds;

        /// The class bit of the placement, `GpuInstance::mClass`: a camera that does not draw the
        /// class is told nothing of it.
        uint mClass;
    };

    struct GpuMaterial
    {
        uint mDiffuse;

        /// The alpha test's reference, which the sides in `mFlags` (`MATERIAL_ALPHA_PASSES_SHIFT`) pass
        /// on. The mode does not survive the trip: a cutout costs traversal one comparison and a
        /// material that wants none passes on every side; the build settles which instances stop.
        float mAlphaReference;

        /// How much of the surface is there, or one for a surface that is all there. **The mode does
        /// not survive the trip here either**: `Material::isBlended` settles it on the host, so a
        /// blended surface stores the alpha its blend weighs by and every other stores a one nothing
        /// branches on. Multiplied by the texture's alpha at the candidate, as a blend does: a stained
        /// pane's texture says where the lead is and this how much glass. A pane is `MATERIAL_TRANSLUCENT`.
        float mOpacity;

        /// What this material is that no number above says — the `MATERIAL_*` bits. **Beside the
        /// reference and the opacity**: with the diffuse these are what `candidateStops` reads, and in
        /// the row's first 32 bytes they cost one sector a candidate where across the row they cost up
        /// to four (2.424–2.432 ms to 2.407–2.414 on the ship, `./omw bench`).
        uint mFlags RTX_ZERO;

        /// Mesh texture coordinates to this material's, as `uv * xy + zw`. The identity for
        /// everything that does not scroll, which is nearly everything.
        vec4 mTextureTransform;

        /// Where this material's terrain layers are, or a count of zero for a single-textured surface,
        /// which is everything but the ground. **The count is also what says a hit is ground**: only
        /// terrain is given layers, so nothing else in the row has to state the kind.
        uint mLayerOffset;
        uint mLayerCount;

        /// A map of what glows and how much, or `NO_TEXTURE`. Added past the albedo rather than
        /// through it, which is where the original engine adds it.
        uint mEmissive;

        /// What the texture is tinted by. **Three channels and not the material's four**: its alpha
        /// is `mOpacity` above, already resolved against the mode, and a second copy of it here would
        /// be a number the shader never reads.
        vec3 mDiffuseColour;

        /// What the texture is tinted by under the fill and at a path's end — `Rtx::Material::
        /// mAmbientColour`.
        vec3 mAmbientColour;

        /// How much the surface glows regardless of what falls on it, with the material's own
        /// multiplier folded in. **A lighting term, not a colour beside one**: the original engine sums
        /// it with the diffuse and ambient light and multiplies the whole by the texture, so a mushroom
        /// cap carrying half glows *with its texture in it*; added past the albedo it comes out flat white.
        vec3 mEmissiveColour;

        /// A sphere-mapped sheet added past the albedo, indexed by where the eye is, or
        /// `NO_TEXTURE`; and what it is tinted by. `Rtx::Material::mEnvironment` says what it is.
        uint mEnvironment;
        vec3 mEnvironmentColour;

        /// A map the albedo is multiplied by, or `NO_TEXTURE`, read at the unit `mFlags` names.
        uint mDark;

        /// A tangent-space normal map, or `NO_TEXTURE`. Read at the diffuse's own place on the sheet (a
        /// companion map has no coordinates of its own and reads unit nought's) through the mesh's tangents.
        uint mNormal;

        /// A `_spec` map in the metal and roughness layout, or `NO_TEXTURE`: metalness in red, perceptual
        /// roughness in green, occlusion in blue. **What names it also says the diffuse was authored as
        /// an albedo**, so such a material is not delit (`Rtx::SpecularLayout`).
        uint mSpecular;

        /// The heights the normal map's relief spans, where `MATERIAL_PARALLAX` says its alpha is one
        /// (`Rtx::Material::mRelief`): the lowest alpha in the low byte and the highest in the byte over
        /// it, which bound the march (`reliefOf`). The row's last word: it stands on sixteen at 112 bytes
        /// and the candidate's fields above share one 32-byte sector.
        uint mRelief RTX_ZERO;
    };

    // **The host's layout has to be the one the device reads**: this side writes these buffers, and a
    // padding byte nobody asked for gives a plausible wrong image rather than an error. GLSL is
    // pinned separately, by the `--scalar-block-layout` the build hands the validator.
#ifdef RTX_HOST
    static_assert(sizeof(GpuVertexColour) == 8, "GpuVertexColour must be scalar-packed on every side");
    static_assert(sizeof(GpuMesh) == 28, "GpuMesh must be scalar-packed on every side");
    static_assert(sizeof(GpuInstance) == 160, "GpuInstance must be scalar-packed on every side");
    static_assert(INSTANCE_LAMP_BODY > (MASK_EVERY_CLASS | MASK_ADDITIVE | MASK_MEDIUM),
        "the lamp body's bit stands above every ray mask's");
    static_assert(sizeof(GpuLight) == 40, "GpuLight must be scalar-packed on every side");
    static_assert(sizeof(GpuLightGrid) == 28, "GpuLightGrid must be scalar-packed on every side");
    static_assert(sizeof(GpuLightCell) == 16, "GpuLightCell must be scalar-packed on every side");
    static_assert(sizeof(GpuLayer) == 64, "GpuLayer must be scalar-packed on every side");
    static_assert(sizeof(GpuMaterial) == 112, "GpuMaterial must be scalar-packed on every side");
    static_assert(sizeof(GpuSprite) == 56, "GpuSprite must be scalar-packed on every side");
    static_assert(sizeof(GpuEmitter) == 40, "GpuEmitter must be scalar-packed on every side");
    static_assert(sizeof(GpuEmitterFrame) == 16, "GpuEmitterFrame must be scalar-packed on every side");
    static_assert(sizeof(GpuPresence) == 24, "GpuPresence must be scalar-packed on every side");
    static_assert(sizeof(GpuTables) == 200, "GpuTables must be scalar-packed on every side");

#endif

#ifdef RTX_HOST
}
#endif

// What both shading languages read and nothing on this side calls: a scalar curve a test has to reach
// goes inside the namespace above, where `RTX_SHADER` makes it `inline` here as well.
#ifndef RTX_HOST

/// Henyey-Greenstein, per steradian: the share of what a medium scatters that leaves `cosine` off
/// the line the light was already travelling. **Shared, because the air and the water both want
/// one**: `fogPhase` blends two to reach Mie's shape and `WATER_ASYMMETRY` is the water's outright.
RTX_SHADER float henyeyGreenstein(float g, float cosine)
{
    const float squared = g * g;
    const float denominator = 1.0 + squared - 2.0 * g * cosine;

    return INV_FOUR_PI * (1.0 - squared) / (denominator * sqrt(denominator));
}

#endif

#endif
