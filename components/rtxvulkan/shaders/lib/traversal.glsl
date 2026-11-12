#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TRAVERSAL_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TRAVERSAL_GLSL

// Traversal, and what a ray found resolved down to the inputs shading needs.
//
// **No light here.** That is what lets water shade by tracing again: a reflection's hit is resolved by this
// same `resolveAsked` and shaded as a solid (`solidLightOf`), and neither calls back into water — which a
// walk with no recursion could not survive.

// A candidate's corners come out of the query itself, which the candidate test builds its plane
// from in the loop, where a vertex fetch would be a table read for every triangle the ray meets.
#extension GL_EXT_ray_tracing_position_fetch : require

#include "shared/traceconstants.h"

#include "gbuffer.h"
#include "look.h"
#include "scene.h"
#include "shared/ground.h"
#include "basis.glsl"
#include "bindings.glsl"
#include "geometry.glsl"
#include "ground.glsl"
#include "random.glsl"
#include "share.glsl"
#include "texturing.glsl"
#include "variants.glsl"

/// The shortest distance to a light worth a ray, in world units: a light nearer the point than this
/// is one nothing can stand between. **Not where a ray starts**, which is `leaveSurface`'s.
const float SHADOW_BIAS = 1.0;

/// How many units in the last place past a hit an eye's ray carries on from, where it carries on along the
/// same line (`alongPast`).
const uint CONTINUATION_ULPS = 64u;

/// Where a ray carries on from along the line it met `distance` along: just past it, by
/// `CONTINUATION_ULPS`.
float alongPast(float distance)
{
    return uintBitsToFloat(floatBitsToUint(distance) + CONTINUATION_ULPS);
}

/// Where a ray leaving a surface along `direction` starts: `point` pushed off its triangle by `step`, on
/// the side `direction` leaves by — and the ray then starts at nought.
///
/// **Not a distance skipped at the start**, which skipped what stood there: a unit of it let the floor at a
/// wall's foot take the sun through the wall and lost every contact shadow nearer than a unit.
///
/// @param step the triangle's unit normal, either way round, times how far the point can stand off
///        the triangle: `stepOf`. Nought for a point in the air, which has no surface to leave and
///        starts where it is.
vec3 leaveSurface(vec3 point, vec3 step, vec3 direction)
{
    return point + (dot(direction, step) < 0.0 ? -step : step);
}

/// Whether a material is meant to be seen through everywhere, rather than in the holes of a mask.
bool isTranslucent(GpuMaterial material)
{
    return (material.mFlags & MATERIAL_TRANSLUCENT) != 0u;
}

/// Whether a material is a blended mask, whose texels under the cut the eye meets by their alpha —
/// `MATERIAL_SOFT_EDGE`, the host's `Material::isSoftEdged`.
bool isSoftEdged(GpuMaterial material)
{
    return (material.mFlags & MATERIAL_SOFT_EDGE) != 0u;
}

/// What a texel of `material` is cut at, for a ray whose dither is `dither`: the material's own reference,
/// or on a soft edge one drawn under it.
///
/// **The eye meets a soft texel under the cut with a chance of its alpha**, stochastic transparency
/// (Enderton et al. 2010): a reference drawn uniformly below the cut, which a texel of alpha `a` under it
/// passes with a chance of `a`, and one at or over it always.
///
/// **Drawn again each frame, not fixed to the surface**, so a pixel's coverage at dense foliage is a mean
/// that only the upscaler's history takes, and a moving frame, which keeps less of it, is noisier than its
/// frames averaged.
///
/// @param dither in `[0, 1)` for the eye's own candidates, drawn a pixel, a frame and a triangle
///        apart so two soft layers are met apart; one for every other ray, which keeps the cut.
float cutAt(GpuMaterial material, float dither)
{
    return isSoftEdged(material) ? clamp(dither, ALPHA_PANE_CUTOFF, material.mAlphaReference)
                                 : material.mAlphaReference;
}

/// Which sides of `GpuMaterial::mAlphaReference` a texel passes on.
uint alphaPassesOf(GpuMaterial material)
{
    return (material.mFlags >> MATERIAL_ALPHA_PASSES_SHIFT) & ALPHA_PASSES_ALL;
}

/// Whether a material carries a mask a ray is tested against: a test some texel can fail. A textured pane
/// carries one too, `ALPHA_PANE_CUTOFF`: a texel its blend draws as nothing is a hole to every ray.
bool hasMask(GpuMaterial material)
{
    return alphaPassesOf(material) != ALPHA_PASSES_ALL;
}

/// Whether a material is a medium the ray goes through rather than a surface it can stop on.
bool isMedium(GpuMaterial material)
{
    return (material.mFlags & MATERIAL_MEDIUM) != 0u;
}

/// How much of a surface is there, before its texture is read.
///
/// **The material's own alpha and the placement's fade**, which are two different facts: every placement of
/// a model shares its material, and a fade belongs to one actor.
float surfaceOpacity(GpuInstance instance, GpuMaterial material)
{
    return instance.mOpacity * material.mOpacity;
}

/// Which faces traversal shows a ray, from whether the ray draws the picture.
///
/// **A ray that draws shows what the rasterizer shows.** OpenMW draws the world with `GL_CULL_FACE` on, so
/// the content states which face of a surface is there, and a ray standing in for that pass states it the
/// same way. A bounce, which carries light, meets a surface from either side instead: a wall met from
/// behind that stopped nothing would leak the light behind it.
uint facingFor(bool draws)
{
    return draws ? gl_RayFlagsCullBackFacingTrianglesEXT : gl_RayFlagsNoneEXT;
}

/// Whether what is behind a surface of this opacity is meant to show through it.
bool isSeenThrough(float opacity)
{
    return opacity < 1.0;
}

/// Whether what is behind a placement of `material` may show through it, before its texture is read: a fade
/// or the material's own alpha below one, or a pane whose texture says how much of it is there.
///
/// @param opacity `surfaceOpacity` of the placement, made once by the caller.
bool isSeenThrough(float opacity, GpuMaterial material)
{
    return isSeenThrough(opacity) || isTranslucent(material);
}

/// How much of a see-through surface is there, where a ray met it.
///
/// **The texture's alpha over the two `surfaceOpacity` carries**, as a blend does: the texture says where a
/// stained pane's lead is, the material how much glass there is, and the placement how much of the whole the
/// game is showing.
///
/// **One function, so that one surface cannot be hazed two ways**: what a shadow ray let past and what the
/// eye covered are the same number seen from either side, and stay so only while it is worked out in one
/// place.
///
/// @param opacity what `surfaceOpacity` gave for this hit, made once by the caller.
/// @param painted the texture's own alpha where the ray met it.
float sampledOpacity(float opacity, float painted)
{
    return clamp(painted * opacity, 0.0, 1.0);
}

/// The same, for a caller that wants nothing of the texel but this. `gatherAlong` wants its colour beside
/// it, and `candidateStops` its alpha against the cutoff.
///
/// @param point where the hit lands on the material's own texture, made once by the caller.
float sampledOpacity(float opacity, GpuMaterial material, TexturePoint point)
{
    return sampledOpacity(opacity, sampleDiffuse(material.mDiffuse, point).a);
}

/// What a see-through candidate keeps from a ray, as the term an order-free sum carries: the logarithm of
/// what it lets past, so that the product of the surfaces is the sum of the terms.
///
/// **Clamped at twenty-four binary orders**, which is a surface nothing measurable gets through and the
/// finest step an eight-bit alpha can name, so that an opaque texel is a large finite term and not an
/// infinity the sum could not hold.
uint blockedBy(float opacity)
{
    return sharePart(-log2(max(1.0 - opacity, exp2(-24.0))));
}

/// What a sum of `blockedBy` terms lets through.
float throughBlocked(uint blocked)
{
    return exp2(-shareTotal(blocked));
}

/// Where on its material's texture a candidate was crossed, at the level `coneWidth` resolves: what the
/// cutout test reads its alpha through and a medium's crossing its texel.
///
/// @param detailed whether the candidate is read along its footprint, as `texturePoint` says.
TexturePoint candidatePoint(
    uvec3 corners, GpuMaterial material, vec2 bary, SurfaceCone cone, float coneWidth, bool detailed)
{
    vec2 uv[3];
    triangleUvs(corners, uv);
    return texturePoint(uv, bary, material.mTextureTransform, cone, coneWidth, detailed);
}

/// Where a map bound at `unit` reads the hit: at the diffuse's own `point`, or on the second set,
/// untransformed, where the mesh says that unit reads it — `GpuMesh::mUnitStreams`.
TexturePoint pointAtUnit(uint unit, TexturePoint point, GpuMesh mesh, uvec3 corner, vec2 bary, SurfaceCone cone,
    float coneWidth, bool detailed)
{
    if (!readsSecondUvs(mesh, unit))
        return point;

    vec2 second[3];
    triangleSecondUvs(mesh, corner, second);
    return texturePoint(second, bary, vec4(1.0, 1.0, 0.0, 0.0), cone, coneWidth, detailed);
}

/// What the dark map paints where a ray crosses `material`, at the unit the content bound it at, and white
/// where it holds none: what the diffuse is multiplied by, colour and alpha, which is where `objects.frag`
/// puts it.
vec4 darkAt(GpuMaterial material, GpuMesh mesh, uvec3 corner, vec2 bary, TexturePoint point, SurfaceCone cone,
    float coneWidth, bool detailed)
{
    if (!holdsTexture(material.mDark))
        return vec4(1.0);

    const uint unit = (material.mFlags >> MATERIAL_DARK_UNIT_SHIFT) & MATERIAL_UNIT_MASK;
    return sampleDiffuse(material.mDark, pointAtUnit(unit, point, mesh, corner, bary, cone, coneWidth, detailed));
}

/// **A sphere-mapped sheet, added past the albedo and indexed by where the eye is**, in light, and nothing
/// where `material` holds none.
///
/// @param span what the triangle's vertex normals spread over (`sheetSpanOf`), taken where the caller held
///        them and not fetched again here: a second fetch inside the surface's resolve held the bounce and
///        the landing a wave short on AMD's Windows driver (`docs/rtx/research/2026-11-09-sheet-span.md`).
/// @param shading the normal the surface shows the ray, in world space and unit.
vec3 sheetAt(GpuMaterial material, SheetSpan span, vec3 shading, vec3 direction, SurfaceCone cone, float coneWidth)
{
    if (!holdsTexture(material.mEnvironment))
        return vec3(0.0);

    // The eye space is OpenGL's, looking down its own -Z.
    const vec3 right = frame.mUnitRight;
    const vec3 up = frame.mUnitUp;
    const vec3 forward = frame.mEyes.mWorld.mBasis.mForward;
    const vec3 viewEye = vec3(dot(direction, right), dot(direction, up), -dot(direction, forward));
    const vec3 normalEye = vec3(dot(shading, right), dot(shading, up), -dot(shading, forward));

    // **Its own footprint and not the surface's**, which `spherePoint` says at length: a sheet
    // indexed by the reflection is not read at the level the mesh's coordinates ask for.
    const TexturePoint sheet = spherePoint(span, normalEye, viewEye, cone, coneWidth);
    return SUNLIT_WHITE * sampleDiffuse(material.mEnvironment, sheet).rgb * material.mEnvironmentColour;
}

/// A triangle a ray query met, before anything decides whether it stops the ray: which instance row
/// and primitive, where on the triangle, and how far along the ray.
struct Candidate
{
    uint mInstance;
    uint mPrimitive;
    vec2 mBary;
    float mAt;
};

/// Reads the candidate `query` stands at into a new `candidate`: the one statement of the read, and
/// a macro because a `rayQueryEXT` cannot be a parameter.
#define RTX_READ_CANDIDATE(query, candidate)                                                                \
    Candidate candidate;                                                                                    \
    candidate.mInstance = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false);                      \
    candidate.mPrimitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, false);                          \
    candidate.mBary = rayQueryGetIntersectionBarycentricsEXT(query, false);                                 \
    candidate.mAt = rayQueryGetIntersectionTEXT(query, false);

/// Reads the world edges of the triangle `query`'s candidate stands on into `edges`: its corners in
/// the object's space, carried into the world. **Apart from the candidate**, since only a read
/// through a cone needs them, and a shadow ray has none.
#define RTX_READ_CANDIDATE_EDGES(query, edges)                                                              \
    {                                                                                                       \
        vec3 corners[3];                                                                                    \
        rayQueryGetIntersectionTriangleVertexPositionsEXT(query, false, corners);                           \
        edges = triangleEdges(corners, rayQueryGetIntersectionObjectToWorldEXT(query, false));              \
    }

/// How a ray meets a surface it could see through — a pane, a fading actor, a material's own alpha: one of
/// three rules.
///
/// **Walked past, keeping what it let through**: a ray toward a light, whose answer is a product the order
/// does not change, and an asker of where the picture ends.
const uint MEET_WALK_PAST = 0u;

/// **Met as often as it is there**, and passed otherwise: every ray that commits a hit and shades it —
/// a bounce, a reflection, a refraction. Met every time, a pane the eye sees
/// through stood solid in each of them; met by chance, their mean is the blend the eye draws.
const uint MEET_BY_CHANCE = 1u;

/// **Met as any solid is**: by the eye's rays, whose kernel peels it and composites the layers
/// itself (`visibility.comp`), and by a falling drop's shelter ray, since glass keeps rain off as a
/// roof does.
const uint MEET_AS_SOLID = 2u;

/// A key a ray draws from, made of the ray itself: where it leaves and which way, so two rays a
/// pixel apart or a frame apart draw apart without a pixel's key handed down every traversal.
uint rayKeyOf(vec3 origin, vec3 direction)
{
    const uvec3 from = floatBitsToUint(origin);
    const uvec3 along = floatBitsToUint(direction);
    uint key = 0u;
    for (uint word = 0u; word < 6u; ++word)
    {
        key ^= word < 3u ? from[word] : along[word - 3u];
        key *= 0x9E3779B1u;
        key ^= key >> 15u;
    }
    return key;
}

/// Whether a candidate hit stops the ray, and what it lets past where it does not.
///
/// **One load of the instance and its material, and not three questions asked in turn**: whether a candidate
/// is see-through, what it lets past and whether it landed in a hole are one decision about one surface.
///
/// @param edges the triangle's world edges, `RTX_READ_CANDIDATE_EDGES`, or none for a ray with no
///        cone, whose every read takes the finest level whatever the triangle is (`coneBaseOf`).
/// @param blocked raised by what a see-through candidate kept, in `blockedBy`'s terms. Untouched
///        otherwise.
/// @param meet how a see-through candidate is met — `MEET_WALK_PAST`, `MEET_BY_CHANCE` or
///        `MEET_AS_SOLID`.
/// @param detailed whether the ray draws the picture, so its cutout is read along the footprint the
///        surface it cuts is read along — `texturePoint`.
/// @param dither the eye's draw for a soft edge (`cutAt`), and one for every other ray.
/// @param rayKey the ray's own key, `rayKeyOf`, which `MEET_BY_CHANCE` draws from. Unread otherwise.
bool candidateStops(Candidate candidate, TriangleEdges edges, vec3 direction, float coneWidth, uint meet,
    bool detailed, float dither, uint rayKey, inout uint blocked)
{
    const GpuInstance instance = instanceAt(candidate.mInstance);
    const GpuMaterial material = materialAt(instance.mMaterial);

    const float opacity = surfaceOpacity(instance, material);
    const bool seenThrough = isSeenThrough(opacity, material);
    const bool walkPast = meet == MEET_WALK_PAST && seenThrough;
    const bool byChance = meet == MEET_BY_CHANCE && seenThrough;

    // **Nothing stops on a medium, whatever the ray was asking.** A surface that is nowhere opaque is not a
    // surface: the eye walks through a cloud and commits the mountain behind it, and a bounce does the
    // same. What the cloud looks like is `mediumAlong`'s answer and not a hit's.
    //
    // **Before the mask test and before any texture is read**, because a ray that is not keeping what it
    // passed through has nothing to read one for.
    if (!walkPast && isMedium(material))
        return false;

    // **A medium reaches a ray by `MASK_MEDIUM`, and its class is asked here.** The structure's mask
    // carries the medium bit alone (`InstanceRecord::mMask` says why), so the class test the mask made for
    // every other surface is this one. The one ray that walks past a medium and keeps what it let through
    // is `passageAsk`'s, which casts with the camera's `solidMask`.
    if (walkPast && isMedium(material) && (instance.mClass & solidMask(frame.mRayMask)) == 0u)
        return false;

    // **Met and not tested where there is nothing to test.** A material with no mask arrives here because
    // forcing an instance non-opaque says nothing about its material: an untextured pane is forced for its
    // own alpha, and an actor for the fade its placement carries. Neither promises a mask.
    //
    // **The material and not the placement.** An actor being faded keeps every hole in its mask, because a
    // fade is not a hole.
    if (!walkPast && !byChance && !hasMask(material))
        return true;

    const TexturePoint point = candidatePoint(triangleCorners(meshAt(instance.mMesh), candidate.mPrimitive), material,
        candidate.mBary, surfaceConeAt(edges, direction), coneWidth, detailed);

    // **The diffuse's alpha alone for the cut**, where `objects.frag` multiplies the dark map's in before
    // `alphaTest`.
    const float painted = sampleDiffuse(material.mDiffuse, point).a;

    // **And the dark map's alpha for a see-through hit's coverage**, which the hit's `resolve` multiplies
    // in: one surface hazed one way from either side (`sampledOpacity`).
    float covered = painted;
    if (HAS_DARK_ALPHA && (material.mFlags & MATERIAL_DARK_ALPHA) != 0u)
    {
        const GpuMesh mesh = meshAt(instance.mMesh);
        covered *= darkAt(material, mesh, triangleCorners(mesh, candidate.mPrimitive), candidate.mBary, point,
            surfaceConeAt(edges, direction), coneWidth, detailed).a;
    }

    // **Tested as the rasterizer tests it**, with the alpha raised by a quarter a level the read stands
    // below the finest (`alpha.glsl`'s `coveragePreservingAlphaScale`, on by default): a box filter's level
    // holds the mean of its texels' alpha, and a mean of opaque leaves and holes falls under the reference,
    // so a canopy thinned with distance and the trace's far foliage was thinner than the rasterizer's.
    const float covering = isSoftEdged(material) ? 1.0 : 1.0 + 0.25 * max(coneLod(material.mDiffuse, point), 0.0);
    const bool there = alphaPasses(alphaPassesOf(material), painted * covering, cutAt(material, dither));

    // **A hole is a hole to the ray that walks past as well.** A placement the game is fading makes its
    // cutout see-through, and the eye still passes a texel the test cuts before it peels what is left — so
    // a shadow ray charged by that texel was a shadow of nothing the eye sees. A pane with no mask passes
    // on every side.
    if (walkPast)
    {
        blocked = addShare(blocked, blockedBy(there ? sampledOpacity(opacity, covered) : 0.0));
        return false;
    }

    // **Met as often as it is there**, one draw a ray and a triangle, so the mean of what the rays
    // find behind a pane is the blend the eye draws over it.
    if (byChance)
    {
        uint draws = randomSeed(rayKey + SEED_SEE_THROUGH + candidate.mInstance * 0x9E3779B9u
            + candidate.mPrimitive * 0x85EBCA6Bu);
        return there && randomNext(draws) < sampledOpacity(opacity, covered);
    }

    return there;
}

/// The eye's draw for a soft edge (`cutAt`), a pixel, a frame and a triangle apart: one where the
/// frame does not dither, which cuts every soft edge at its middle.
float softEdgeDither(uint key, Candidate candidate)
{
    if (frame.mSoftEdgeDither == 0u)
        return 1.0;

    uint draws = randomSeed(key + candidate.mInstance * 0x9E3779B9u + candidate.mPrimitive * 0x85EBCA6Bu);
    return randomNext(draws);
}

/// One traversal, of any kind this file asks for — `passageAsk`, `surfaceWithin` and `traceAsk` — as a
/// request: the flags, mask and limits `rayQueryInitializeEXT` takes, and how its candidate loop meets a
/// surface (`candidateStops`).
struct SceneQuery
{
    vec3 mFrom;
    float mTmin;
    vec3 mAlong;
    float mTmax;
    uint mFlags;
    uint mMask;
    uint mMeet;
    uint mDetailed;
    float mConeWidth;
    float mConeSpread;

    /// One where a soft edge is cut at the eye's draw (`softEdgeDither`) and not at its middle: the
    /// eye's own rays, which the pixel's draw dithers a frame at a time. `mDitherKey` is the pixel's
    /// key with `SEED_SOFT_EDGE`.
    uint mDithered;
    uint mDitherKey;
};

/// What a traversal committed, before anything is read off it: whether it committed at all, where,
/// what a ray that walked past let through, and the geometry `committedHit` takes.
struct SceneAnswer
{
    uint mCommitted;
    float mDistance;
    uint mBlocked;
    uint mInstance;
    uint mPrimitive;
    vec2 mBary;

    /// The committed instance's material kind, `KIND_WATER` and the rest: the shader-table offset
    /// `SceneAcceleration::placeRow` writes, read back.
    uint mKind;
};

/// What a step that asked no ray is handed in place of an answer: nothing committed, and nothing
/// walked past.
SceneAnswer unansweredScene()
{
    return SceneAnswer(0u, 0.0, 0u, 0u, 0u, vec2(0.0), 0u);
}

/// **The one traversal loop**, which every query in this file runs through: AMD's Windows compiler writes a
/// whole software traversal at every site that drives one, and a stage that held fifty of them did not
/// compile in 52 GB (`docs/rtx/research/2026-10-19-amd-windows-trace-compile-memory.md`).
///
/// **The candidate loop runs to completion.** It confirms every hit that lands on the material rather than in
/// one of its holes and, where the request asks, walks past a translucent one keeping what it let through
/// (`candidateStops`). The cone at a candidate decides how much of the mask one pixel is looking at; a ray
/// that carries none (every shadow ray) reads the finest level.
///
/// **The committed triangle's corners are not read here** but off the scene's own vertices (`committedHit`):
/// AMD's Windows driver fetches a committed triangle's corners wrong.
SceneAnswer queryScene(SceneQuery asked)
{
    rayQueryEXT query;
    rayQueryInitializeEXT(query, sceneTop, asked.mFlags, asked.mMask, asked.mFrom, asked.mTmin, asked.mAlong, asked.mTmax);

    const uint rayKey = asked.mMeet == MEET_BY_CHANCE
        ? rayKeyOf(rayQueryGetWorldRayOriginEXT(query), rayQueryGetWorldRayDirectionEXT(query))
        : 0u;

    uint blocked = 0u;
    while (rayQueryProceedEXT(query))
    {
        if (rayQueryGetIntersectionTypeEXT(query, false) != gl_RayQueryCandidateIntersectionTriangleEXT)
            continue;

        RTX_READ_CANDIDATE(query, candidate)
        const float coneWidth = asked.mConeWidth + asked.mConeSpread * candidate.mAt;

        // **A branch, measured**: a shadow ray has no cone and reads no edges.
        TriangleEdges edges = TriangleEdges(vec3(0.0), vec3(0.0));
        if (coneWidth > 0.0)
            RTX_READ_CANDIDATE_EDGES(query, edges)
        const float dither = asked.mDithered != 0u ? softEdgeDither(asked.mDitherKey, candidate) : 1.0;
        if (candidateStops(candidate, edges, asked.mAlong, coneWidth, asked.mMeet, asked.mDetailed != 0u, dither,
                rayKey, blocked))
            rayQueryConfirmIntersectionEXT(query);
    }

    SceneAnswer answer = unansweredScene();
    answer.mDistance = asked.mTmax;
    answer.mBlocked = blocked;
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        answer.mCommitted = 1u;
        answer.mDistance = rayQueryGetIntersectionTEXT(query, true);
        answer.mInstance = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
        answer.mPrimitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
        answer.mBary = rayQueryGetIntersectionBarycentricsEXT(query, true);
        answer.mKind = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(query, true);
    }
    return answer;
}

/// A ray one step of a walk leaves for the traversal that answers it, or none: `mTraced` is false where the
/// answer is known without one — a light within `SHADOW_BIAS`, a picture with its shadows off.
///
/// **A request and not a call**, so a walk traces every ray it casts at the one `queryScene` of the loop
/// that steps it: AMD's Windows compiler writes a whole traversal at every such site (`queryScene`).
struct RayAsk
{
    bool mTraced;
    SceneQuery mQuery;
};

/// What came back for a `RayAsk`: whether it was traced, and what the traversal answered where it
/// was.
struct RayAnswer
{
    bool mTraced;
    SceneAnswer mMet;
};

/// The step every walk's record (`SkyPassageRun`, `GatherRun` and the rest) stands at where none of
/// it is under way: the record's answer is then what the walk came to.
const uint RUN_IDLE = 0u;

RayAsk noRay()
{
    return RayAsk(false, SceneQuery(vec3(0.0), 0.0, vec3(0.0, 0.0, 1.0), 0.0, 0u, 0u, 0u, 0u, 0.0, 0.0, 0u, 0u));
}

RayAsk rayOf(SceneQuery query)
{
    return RayAsk(true, query);
}

/// Traces `asked` where it asks a ray, and answers it: a request's synchronous form, for an asker
/// that waits on its one ray where it stands.
RayAnswer answerAsked(RayAsk asked)
{
    return asked.mTraced ? RayAnswer(true, queryScene(asked.mQuery))
                         : RayAnswer(false, unansweredScene());
}

/// What `Hit::mRelief` holds where no march through the hit's relief was stored: `resolve` marches it. Past
/// any march's answer, which a texture's side bounds (`reliefToward`).
const float RELIEF_UNMARCHED = 3.0e38;

/// Where `resolve` takes a hit's relief from, a constant of each call so a stage that reads stored hits
/// compiles no march: `RELIEF_MARCHES`, the trace's, which marches a relief it finds unmarched; or
/// `RELIEF_STORED`, a stage's, whose hit carries the march the trace stored, or is a landing, whose relief
/// is held flat and so is its shift (`reliefToward`).
const uint RELIEF_MARCHES = 0u;
const uint RELIEF_STORED = 1u;

/// What `reliefToward` answers a relief held flat, or met edge on: its shift alone, at `height`.
vec2 reliefShiftOf(float height)
{
    return vec2(height * PARALLAX_SCALE + PARALLAX_BIAS, 0.0);
}

/// The triangle a traversal committed, placed in the world, before any material is read off it.
///
/// **Geometry, and no material**: the triangle off the instance's row and the scene's own vertices
/// (`committedHit`).
struct Hit
{
    bool mHit;

    /// Which row of the instance table this came off, which is the custom index the build wrote and
    /// not the structure's own.
    uint mInstance;

    /// Which row of the mesh table the instance wears, read off the instance here for the corners
    /// and carried out so `resolve` reads the mesh row once and not the instance's for it again.
    uint mMesh;

    /// Where in the shared vertex buffers this triangle's three corners are, already global.
    ///
    /// **Resolved in `committedHit` and carried out, rather than the primitive index**: the index block is
    /// read there anyway, to interpolate the shading normal while the object-to-world matrix is in scope, and
    /// `resolve` would read the same block for the same three numbers. Neutral in time, and kept to avoid
    /// the duplication.
    uvec3 mCorner;

    vec2 mBary;
    float mDistance;

    /// Where the ray met the triangle, in the world: the corners' mix by the weights, carried
    /// through the placement. **Off the triangle and not off the ray**, whose rounding grows with
    /// its length and with how far its origin stands.
    vec3 mPosition;

    /// How far `mPosition` can stand off its triangle along the triangle's normal, either way: the
    /// rounding of the mix, of the placement, and of the card's own test of a ray against the
    /// triangle. What a ray leaving here steps off by (`leaveSurface`).
    float mRounding;

    /// How wide the ray's cone was where it landed.
    float mFootprint;

    /// The triangle's two edges in world space, whose cross product is twice its area along its plane's
    /// normal.
    ///
    /// **Made here rather than carried as three corners**, because nothing past `committedHit`, which reads
    /// the corners off the scene's vertices, wants them.
    TriangleEdges mEdges;

    /// The vertex normal interpolated across the triangle, in world space — or nought where the mesh
    /// carries none, which `resolve` reads as "use the plane".
    ///
    /// **Three floats where the transform they came through is nine**: the object-to-world matrix only brings
    /// this one vector across, so the vector is what a `Hit` carries and not the matrix. Not unit: `resolve`
    /// normalises it, and the uniform scale in the transform drops out there.
    vec3 mShading;

    /// The vertex tangent interpolated across the triangle, in world space, with the bitangent's
    /// handedness in `w` — or nought where the mesh carries none, which is every vanilla mesh.
    /// Brought across here for the reason `mShading` is: the matrix does not survive the call.
    vec4 mTangent;

    /// How the vertex normal at the second corner and at the third differs from the first's, in world space
    /// and not unit, as `mShading` is interpolated from them — nought where the mesh carries no normals.
    vec3 mSecondTurn;
    vec3 mThirdTurn;

    /// How far toward the eye a march through the hit's relief already showed its texture
    /// (`Surface::mRelief`), which a stage reads back off the trace's (`storedHitOf`), or
    /// `RELIEF_UNMARCHED` for `resolve` to march.
    float mRelief;

    /// Whether a relief nothing marched is marched, or shifted once: shifted at a bounce's landing, a
    /// reflection's, as RTX Remix leaves an indirect hit's relief unmarched — marched there, the landing's
    /// stage cost the close wall 0.13 ms of a 6.4 ms frame (`research/2026-11-08-relief-cost-and-noise.md`).
    bool mMarches;
};

/// A ray that committed nothing, as far away as anything can be.
Hit noHit()
{
    Hit hit;
    hit.mHit = false;
    hit.mInstance = 0u;
    hit.mMesh = 0u;
    hit.mCorner = uvec3(0u);
    hit.mBary = vec2(0.0);
    hit.mDistance = frame.mReach;
    hit.mPosition = vec3(0.0);
    hit.mRounding = 0.0;
    hit.mFootprint = 0.0;
    hit.mEdges = TriangleEdges(vec3(0.0), vec3(0.0));
    hit.mShading = vec3(0.0);
    hit.mTangent = vec4(0.0);
    hit.mSecondTurn = vec3(0.0);
    hit.mThirdTurn = vec3(0.0);
    hit.mRelief = RELIEF_UNMARCHED;
    hit.mMarches = true;

    return hit;
}

/// Where a hit stands and how far that can be off its triangle: `Hit::mPosition` and `Hit::mRounding`.
///
/// **NVIDIA's bound for a hardware traversal** (*Solving Self-Intersection Artifacts in DirectX Raytracing*,
/// NVIDIA developer blog, 2022), the one that holds on a card whose own ray-triangle test rounds: the point
/// is mixed in the mesh's own space, base corner last, and carried into the world with the translation last;
/// its error is bounded in the mesh's space by the corner's size and the triangle's extent (`c1` covers
/// RTX's test, and the article says the bound is the hardware's) and in the world by the placement's.
void placeHit(inout Hit hit, vec3 corners[3], vec2 bary, mat4x3 toWorld, mat4x3 toObject)
{
    const float c0 = 5.9604644775390625e-8;
    const float c1 = 1.788139769587360206060111522674560546875e-7;
    const float c2 = 1.19209317972490680404007434844970703125e-7;

    const vec3 first = corners[1] - corners[0];
    const vec3 second = corners[2] - corners[0];
    const vec3 local = corners[0] + (first * bary.x + second * bary.y);
    const mat3 turn = mat3(toWorld);
    const vec3 placed = toWorld[3] + (turn[0] * local.x + (turn[1] * local.y + turn[2] * local.z));
    hit.mPosition = placed;

    const vec3 crossed = cross(first, second);
    const vec3 sizes = abs(first) + abs(second) + abs(abs(first) - abs(second));
    const float extent = max(max(sizes.x, sizes.y), sizes.z);
    const mat3 back = mat3(toObject);
    const vec3 localError = c0 * abs(corners[0]) + c1 * extent
        + c2 * (mat3(abs(back[0]), abs(back[1]), abs(back[2])) * abs(placed) + abs(toObject[3]));
    const vec3 placedError
        = c1 * (mat3(abs(turn[0]), abs(turn[1]), abs(turn[2])) * abs(local)) + c2 * abs(toWorld[3]);

    const vec3 normal = transpose(back) * crossed;
    const float lengthSquared = dot(normal, normal);
    const float scale = lengthSquared > 0.0 ? inversesqrt(lengthSquared) : 0.0;
    hit.mRounding = scale * dot(localError, abs(crossed)) + dot(placedError, abs(normal * scale));
}

/// How wide `asked`'s cone was where it met what `met` holds.
float footprintOf(SceneQuery asked, SceneAnswer met)
{
    return asked.mConeWidth + asked.mConeSpread * met.mDistance;
}

/// The committed intersection, read off the query and put into world space.
///
/// **Its corners off the scene's own vertices** (`trianglePositions`), the numbers its structure was built
/// over, where a query's position fetch would read them: AMD's Windows driver fetches a committed triangle's
/// corners wrong, another triangle's along the ray or a confirmed one's turned
/// (`docs/rtx/research/2026-10-19-amd-windows-driver-faults.md`).
///
/// **Its transforms off the instance's row** (`GpuInstance::mToWorld`, `mToObject`), the top level's own and
/// its inverse and not the query's: a stage resolves a hit the trace stored with no query at all
/// (`storedSolidOf`), and the two must read the same numbers.
Hit committedHit(uint instance, uint primitive, vec2 bary, float distance, float footprint)
{
    Hit hit;
    hit.mHit = true;
    hit.mInstance = instance;
    hit.mBary = bary;
    hit.mDistance = distance;
    hit.mFootprint = footprint;
    hit.mRelief = RELIEF_UNMARCHED;
    hit.mMarches = true;

    // **The vertex fetches a hit takes, here so that the transform need not survive the call.** The test is
    // on the mesh's own normal rather than on the transformed one: a mesh with no normals stores zeros, and
    // a scale that shrank a real normal past the threshold would otherwise change which branch it took.
    const GpuInstance placement = instanceAt(instance);
    const mat4x3 toWorld = toWorldOf(placement);
    const mat4x3 toObject = toObjectOf(placement);
    hit.mMesh = placement.mMesh;
    const GpuMesh mesh = meshAt(hit.mMesh);
    hit.mCorner = triangleCorners(mesh, primitive);

    vec3 corners[3];
    trianglePositions(mesh, hit.mCorner, corners);
    hit.mEdges = triangleEdges(corners, toWorld);
    placeHit(hit, corners, bary, toWorld, toObject);

    vec3 normals[3];
    triangleNormals(mesh, hit.mCorner, normals);
    const vec3 shading = acrossTriangle(normals[0], normals[1], normals[2], bary);
    const bool carried = dot(shading, shading) > 1e-8;
    hit.mShading = carried ? mat3(toWorld) * shading : vec3(0.0);
    hit.mSecondTurn = carried ? mat3(toWorld) * (normals[1] - normals[0]) : vec3(0.0);
    hit.mThirdTurn = carried ? mat3(toWorld) * (normals[2] - normals[0]) : vec3(0.0);

    // **Three more words, and only off a mesh that carries them.** The bit is on the row this already
    // read, so a hit on anything no normal map is read through — every hit in a vanilla scene —
    // pays one test and no fetch.
    hit.mTangent = vec4(0.0);
    if (HAS_MAPS && (mesh.mShape & MESH_TANGENTS) != 0u)
    {
        const vec4 tangent = triangleTangent(mesh, hit.mCorner, bary);
        hit.mTangent = vec4(mat3(toWorld) * tangent.xyz, tangent.w);
    }

    return hit;
}

/// Whether a ray met something, where the hit stands and how far it can stand off its triangle, the cone's
/// width there and the distance: the part of `committedHit` a walk that only leaves a surface or measures how
/// far it went reads (`stepWater`), with none of the normals or the tangent a `Surface` is resolved from.
///
/// **So a walk carries no `Surface` through the traversals it asks**: a resolved surface held across a ray
/// query is twenty-odd scalars more of every live state, which held the water's kernel past its ceiling on
/// AMD's Windows driver (248 registers of 244).
struct PlacedHit
{
    bool mHit;
    vec3 mPosition;
    float mRounding;
    float mFootprint;
    float mDistance;
};

/// Where `met` stands, as `committedHit` places it: the same corners, the same placement and the same
/// rounding, and nothing it reads beyond them.
PlacedHit placedHitOf(SceneAnswer met, SceneQuery asked)
{
    PlacedHit placed = PlacedHit(false, vec3(0.0), 0.0, 0.0, 0.0);
    if (met.mCommitted == 0u)
        return placed;

    const GpuInstance placement = instanceAt(met.mInstance);
    const GpuMesh mesh = meshAt(placement.mMesh);
    vec3 corners[3];
    trianglePositions(mesh, triangleCorners(mesh, met.mPrimitive), corners);
    Hit hit = noHit();
    placeHit(hit, corners, met.mBary, toWorldOf(placement), toObjectOf(placement));
    return PlacedHit(true, hit.mPosition, hit.mRounding, footprintOf(asked, met), met.mDistance);
}

/// Whether `met` met something and how far: all a walk's step past its start reads of a ray's answer.
PlacedHit reachOf(SceneAnswer met)
{
    return PlacedHit(met.mCommitted != 0u, vec3(0.0), 0.0, 0.0, met.mDistance);
}

/// What a ray toward a light met: whether a surface stopped it outright, and what the surfaces it crossed
/// let through.
///
/// **Two numbers, because two filters want them apart.** Whether the ray was stopped is the one bit a
/// shadow denoiser filters (`CHANNEL_SHADOWED`, `CHANNEL_LAMPED`), and what a leaf or a pane let through is
/// a fraction the same ray measures exactly — so it stays in the light the bit multiplies, and nothing
/// blurs it.
struct Passage
{
    /// One where nothing opaque stood in the way, and nought where something did.
    float mOpen;

    /// What the translucent surfaces crossed let through, from nought to one.
    float mThrough;

    /// How far along the ray the solid that stopped it stood, or `SHADOW_PENUMBRA_CLEAR` where nothing did:
    /// what the penumbra's width is made of (`skyPenumbra`, `lampPenumbra`).
    float mOccluder;
};

/// What a light nothing stands in front of answers.
Passage openPassage()
{
    return Passage(1.0, 1.0, SHADOW_PENUMBRA_CLEAR);
}

/// What the ray from `from` to what stands `distance` away along `towards` meets, past the surfaces whose
/// faces `faces` does not cull (`skyLightAsk`, `lampLightAsk` and `ambientAsk` say which), as the ray a walk
/// asks, and `passageAnswered` what it answered.
///
/// **No cone, so level zero.** JCGT 10(1) 2021 finds level zero slower than a cone level in every scene it
/// tries, but its finding is about the *fetch* and this path's cost is the *level*: a width of nought is
/// answered at once, skipping the texel count's load in `coneLod` and a determinant and two logarithms in
/// `coneBase` at every candidate, which saves more than the cache gives back on every place tried.
///
/// **A light nearer than `SHADOW_BIAS` is open.** A candle on a table asks for a ray of next to no length,
/// and one of none or less is a `tmax` under the `tmin`, which `rayQueryInitializeEXT` leaves undefined: a
/// hang or a garbage answer rather than an empty one. Nothing stands between a point and a light that near.
///
/// **A translucent surface dims the light rather than stopping it**, and the order it is met in does not
/// matter: the answer is a product, which makes the shadow the cheap half of transparency (the eye needs its
/// layers sorted). Taken as `blockedBy`'s sum and not as the product itself, because a float product does
/// care.
///
/// **`TerminateOnFirstHit` stays, but where the occluder's distance is read.** A translucent candidate is
/// never confirmed, so traversal walks past it and keeps the early out for the first thing that does stop the
/// ray. The first in traversal's order is not the nearest, and a penumbra made of a roof five hundred units
/// on blurred away the contact shadow of a hand two units over a table; so a ray whose penumbra the shadow
/// denoiser reads runs to the nearest solid.
///
/// @param from where the ray starts, already off its surface (`leaveSurface`).
/// @param faces the ray flags that cull one face or none.
/// @param nearest whether `Passage::mOccluder` must be the nearest solid's — a split ray's, whose
///        penumbra is read.
RayAsk passageAsk(vec3 from, vec3 towards, float distance, uint faces, bool nearest)
{
    if (distance <= SHADOW_BIAS)
        return noRay();

    // **And the mediums**, which only this ray and `mediumAlong` meet: a medium dims the light that
    // crosses it, and `candidateStops` asks its class in place of the mask.
    return rayOf(SceneQuery(from, 0.0, towards, distance,
        (nearest ? gl_RayFlagsNoneEXT : gl_RayFlagsTerminateOnFirstHitEXT) | faces,
        solidMask(frame.mRayMask) | MASK_MEDIUM, MEET_WALK_PAST, 0u, 0.0, 0.0, 0u, 0u));
}

/// What a `passageAsk` met: open where it asked no ray.
Passage passageAnswered(RayAnswer answered)
{
    if (!answered.mTraced)
        return openPassage();

    const bool stopped = answered.mMet.mCommitted != 0u;
    return Passage(stopped ? 0.0 : 1.0, stopped ? 1.0 : throughBlocked(answered.mMet.mBlocked),
        stopped ? answered.mMet.mDistance : SHADOW_PENUMBRA_CLEAR);
}

/// `passageAnswered`'s two halves as one number. **The product is exact**: the open half is nought
/// or one, so it either is the other half or is nought, and every caller reads what it read before
/// the two were apart.
float throughAnswered(RayAnswer answered)
{
    const Passage passage = passageAnswered(answered);
    return passage.mOpen * passage.mThrough;
}

/// What the ray from `from` toward the sun or a moon along `towards` meets, `distance` long, in `Passage`'s
/// two halves, and the nearest solid's distance where `nearest` asks (`passageAsk`).
///
/// **Every face stops it, which is the rasterizer's rule.** Its shadow map is drawn with `GL_CULL_FACE` off
/// unless `[Shadows] use front face culling` asks otherwise, and with it on it keeps the faces turned away
/// from the light: under either, a single-sided wall casts whichever way it faces. The content leans on it:
/// a quasi-exterior is an interior's shell, which faces in, stood under the sun.
RayAsk skyLightAsk(vec3 from, vec3 towards, float distance, bool nearest)
{
    return passageAsk(from, towards, distance, gl_RayFlagsNoneEXT, nearest);
}

/// The same for a lamp `distance` away along `towards`.
///
/// **Only a face turned away from the point casts**, so the ray culls the front faces.
RayAsk lampLightAsk(vec3 from, vec3 towards, float distance, bool nearest)
{
    return passageAsk(from, towards, distance, gl_RayFlagsCullFrontFacingTrianglesEXT, nearest);
}

/// The ray asking how much of the ambient along `towards` reaches `from`, past whatever stands within
/// `distance`: `throughAnswered` of it.
///
/// **Every face stops it, the one turned toward `from` included.** What this asks is not a lamp's question
/// but whether anything stands over the point — the pillow over the sheet, the floor under a lid — so a
/// surface met from the side it faces stands over the point as much as one met from behind.
RayAsk ambientAsk(vec3 from, vec3 towards, float distance)
{
    return passageAsk(from, towards, distance, gl_RayFlagsNoneEXT, false);
}

/// A ray in the world: where it leaves from and which way it goes, unit. `Ray` in `camera.h` is a
/// pixel's ray relative to the eye; this is one any shader traces from anywhere.
struct WorldRay
{
    vec3 mFrom;
    vec3 mAlong;
};

/// Which surfaces an asker's ray meets and how, for `surfaceWithin`.
struct RayRule
{
    /// Which instances stop the ray.
    uint mMask;

    /// How a surface the eye would see through is met: walked past, `MEET_WALK_PAST`, which is the eye's
    /// own rule — `visibility.comp` peels those and commits what stands behind them — or by chance,
    /// `MEET_BY_CHANCE`.
    uint mMeet;

    /// The same division again, and the same two askers — `facingFor`. Where the picture ends is where
    /// the eye's own ray ends, and the eye culls.
    bool mDraws;
};

/// How far the nearest surface that stops a ray is along it, at most `reach` away.
///
/// **Traversal and the cutout, and no material resolved at all.** An asker that wants a distance would pay
/// for the whole of `resolveAsked` otherwise (the plane, the shading normal, the emissive, and for ground the
/// entire layer stack with a mask read apiece) to read one float back. The cutout still runs, or the ray
/// would stop in the holes of a mask.
///
/// **`reach` is the answer as well as the limit**, which makes such a ray short: an asker that only cares
/// whether anything stands within a band hands over the band, and reads a miss as *no nearer than that*.
///
/// @param rule which surfaces stop the ray and how it meets them — `RayRule`. `solidWithinAsk` asks
///        for solids alone, met as nearest things and from either face.
SceneQuery surfaceWithinQuery(WorldRay ray, float tmin, float reach, Cone cone, RayRule rule)
{
    return SceneQuery(ray.mFrom, tmin, ray.mAlong, reach, facingFor(rule.mDraws), rule.mMask, rule.mMeet,
        rule.mDraws ? 1u : 0u, cone.mWidth, cone.mSpread, 0u, 0u);
}

/// What a `surfaceWithinQuery` with this `reach` answered.
float withinOf(SceneAnswer met, float reach)
{
    return met.mCommitted != 0u ? met.mDistance : reach;
}

float surfaceWithin(WorldRay ray, float tmin, float reach, Cone cone, RayRule rule)
{
    return withinOf(queryScene(surfaceWithinQuery(ray, tmin, reach, cone, rule)), reach);
}

RayAsk solidWithinAsk(WorldRay ray, float tmin, float reach, Cone cone)
{
    return rayOf(surfaceWithinQuery(ray, tmin, reach, cone, RayRule(solidMask(frame.mRayMask), MEET_BY_CHANCE, false)));
}

/// What a ray found, resolved down to the inputs shading needs.
struct Surface
{
    bool mHit;

    /// Whether what the ray met is the ground itself rather than something standing on it.
    ///
    /// **What `BOUNCE_REACH` is allowed to hand the sky.** A draw about open ground reaches it whatever
    /// stands nearby; the same draw about a wall spends half of itself on whatever the wall belongs to. A
    /// material with a layer stack is ground, and only ground has one.
    bool mGround;

    vec3 mPosition;

    /// The shading normal, turned to the side of the triangle's plane the ray arrived on.
    vec3 mNormal;

    /// The interpolated normal, turned as `mNormal` is, before any map: `mNormal` wherever there is no map.
    vec3 mSmooth;

    /// Which way the ray that found this surface was travelling, which is what the lobe is
    /// evaluated against: the eye's own ray, a bounce, a reflection.
    vec3 mIncident;

    /// How far `mPosition` can stand off its triangle: `Hit::mRounding`.
    float mRounding;

    /// The triangle's own plane, turned the same way `mNormal` is.
    ///
    /// **What a bounce is bounded by, and what a sheet takes a light's side from.** A shading normal leans off
    /// its own triangle wherever the content faceted a curve, so it would aim a bounce into the floor the
    /// bounce left; the plane does not. It is turned rather than left as the winding wound it, so a caller
    /// has one vector meaning "out of this surface" and no side of its own to work out.
    vec3 mGeometric;

    /// The diffuse albedo: the texture's, delit, for a vanilla surface, and `(1 - metal)` of the
    /// authored base colour for one with a specular map.
    vec3 mAlbedo;

    /// The same texture under the ambient colour, `GpuMaterial::mAmbientColour`: what the fill and
    /// the ambient at a path's end are reflected by, as the rasterizer reflects its ambient term.
    /// Changed after the tint wherever `mAlbedo` is, so the two part only by their colours.
    vec3 mAmbientAlbedo;

    /// The reflectance at normal incidence, `F0`: nought for a vanilla surface, which reflects
    /// nothing — `DIELECTRIC_F0` says why — and the base colour's mix toward `DIELECTRIC_F0` by the
    /// map's metalness for one with a specular map.
    vec3 mSpecular;

    /// The perceptual roughness the map paints, and one where there is no map, which is what a
    /// Lambert surface is.
    float mRoughness;

    /// How much of the indirect light the relief a specular map paints lets in: its blue, and one where
    /// there is none. Past `mAlbedo` and `mAmbientAlbedo` because neither says it: a lamp and the sun light
    /// the same crack by them, and Night-Eye's lift is held to no occlusion. `occludedAlbedo` applies it.
    float mOcclusion;

    /// The material's own glow, as a lighting term. See `GpuMaterial::mEmissiveColour`.
    vec3 mEmissiveColour;

    /// What its emissive map adds past the albedo, already scaled.
    vec3 mEmitted;

    float mDistance;

    /// Which row of the instance table this came off, so the frame can ask where it used to be.
    uint mInstance;

    /// How wide the ray's cone was where it landed. Everything sampled here was averaged over it,
    /// and so is everything the light arriving here was.
    float mFootprint;

    /// How much of this surface is there, where the ray met it. One for everything that is all there, which
    /// is nearly everything.
    float mOpacity;

    /// Whether the content doubled this mesh for its back — `MESH_SHEET` — and so lit it from both
    /// faces: what takes a light's side from `mGeometric` and not from `mSmooth` (`facingOf`).
    bool mSheet;

    /// How far above this point the surface the vertex normals describe stands — `smoothLift` — which every
    /// light's shadow ray leaves from.
    ///
    /// **On every shadow ray, as Hanika and RTX Remix lift it**, and not only past the facet's terminator
    /// as Cycles does.
    vec3 mLift;

    /// What light on the far side of this surface is worth to the side the ray met, against the same light
    /// on the near side. Nought for everything solid; `SHEET_TRANSMISSION` for a leaf.
    ///
    /// **Two facts and neither alone is a leaf.** The mesh says the content doubled it for its back —
    /// `MESH_SHEET` in `GpuMesh::mShape` — and the material says it carries a mask. A tabard is doubled and
    /// has none, and is cloth lit from the side it is seen from; a pane carries a mask and is not doubled,
    /// and passes light by its opacity rather than by this.
    float mTransmission;

    /// Whether the lamps light this surface: false under `MATERIAL_NO_LAMPS`, where `weighLamps`
    /// and `darkeningAt` walk an empty run.
    bool mLampLit;

    /// How far toward the eye its relief showed its texture, in the texture's own units along the eye's
    /// lean in the map's tangent frame (`reliefToward`): the march's answer, or the shift's where the march
    /// took no step, and nought where no relief was read, which the trace stores for the stages after it
    /// (`storeStageSolid`).
    float mRelief;

    /// Where its relief showed the eye its texel, which a light's march through it starts from (`reliefLit`),
    /// or none.
    ReliefSite mReliefSite;

    /// How much farther than `mDistance` along the ray the texel its relief showed stands, in the world
    /// (`reliefDepthOf`), less than nought where the texel stands over the triangle. What the eye's motion and
    /// its distance are measured to (`visibility.comp`); nought where the march took no step, so a relief a
    /// tuple marches and one it shifts stand at one distance where both shift, and nought off a stored hit,
    /// whose motion its trace measured.
    float mReliefDepth;
};

/// A ray that met nothing, as far away as anything can be: what `resolveAsked` answers with for a
/// miss, and what `resolve` fills in from for a hit.
Surface noSurface(vec3 origin)
{
    Surface surface;
    surface.mHit = false;
    surface.mGround = false;
    surface.mPosition = origin;
    surface.mRounding = 0.0;
    surface.mNormal = vec3(0.0, 0.0, 1.0);
    surface.mSmooth = vec3(0.0, 0.0, 1.0);
    surface.mIncident = vec3(0.0, 0.0, -1.0);
    surface.mGeometric = vec3(0.0, 0.0, 1.0);
    surface.mAlbedo = vec3(0.0);
    surface.mAmbientAlbedo = vec3(0.0);
    surface.mSpecular = vec3(0.0);
    surface.mRoughness = 1.0;
    surface.mOcclusion = 1.0;
    surface.mEmissiveColour = vec3(0.0);
    surface.mEmitted = vec3(0.0);
    surface.mDistance = frame.mReach;
    surface.mInstance = 0u;
    surface.mFootprint = 0.0;
    surface.mOpacity = 1.0;
    surface.mSheet = false;
    surface.mLift = vec3(0.0);
    surface.mTransmission = 0.0;
    surface.mLampLit = true;
    surface.mRelief = 0.0;
    surface.mReliefSite = noReliefSite();
    surface.mReliefDepth = 0.0;

    return surface;
}

/// The step a ray leaving `surface` takes off its triangle, for `leaveSurface`: the plane times how
/// far the point can stand off it.
vec3 stepOf(Surface surface)
{
    return surface.mGeometric * surface.mRounding;
}

/// What a hit is made of.
///
/// **Everything a hit leads to and nothing the traversal already answered.** Every table this reads is keyed
/// on where the ray landed (the instance, its mesh, its material, its textures), and `Hit` is what the
/// traversal answered.
///
/// @param detailed whether the hit is a picture: the eye's own, a reflection's, the bed under a
///        waterline — `traceAsk`'s `draws`. **A diffuse bounce's far hit is not**, and reads no normal
///        map and no parallax: what it sends back is averaged over a hemisphere and then filtered,
///        and relief read there moved nothing a 1024-frame reference could tell from its own noise,
///        at Balmora or in the census office. The albedo, the reflectance and the roughness are read
///        either way, because the energy the hit sends back is theirs.
/// @param reliefSource `RELIEF_MARCHES` or `RELIEF_STORED`, a constant of the call.
Surface resolve(Hit hit, vec3 origin, vec3 direction, bool detailed, uint reliefSource)
{
    Surface surface = noSurface(origin);

    if (!hit.mHit)
        return surface;

    surface.mHit = true;
    surface.mDistance = hit.mDistance;
    surface.mPosition = hit.mPosition;
    surface.mRounding = hit.mRounding;
    surface.mIncident = direction;

    surface.mFootprint = hit.mFootprint;

    surface.mInstance = hit.mInstance;

    const GpuInstance instance = instanceAt(surface.mInstance);
    const GpuMesh mesh = meshAt(hit.mMesh);
    const uvec3 corner = hit.mCorner;

    // The plane of the corners `committedHit` read, where the vertices' own normals are a fetch and
    // are better where they are.
    const vec3 crossed = cross(hit.mEdges.mFirst, hit.mEdges.mSecond);
    surface.mGeometric = dot(crossed, crossed) > 0.0 ? normalize(crossed) : vec3(0.0, 0.0, 1.0);

    // Every texture read below shares this hit's triangle and this ray: a chunk's whole layer stack,
    // the opacity a pane pays for, and the emissive map.
    const SurfaceCone cone = surfaceConeAt(hit.mEdges, direction);

    const vec3 normal = dot(hit.mShading, hit.mShading) > 0.0 ? normalize(hit.mShading) : surface.mGeometric;

    // What the footprint averages of the mesh's own bend, and below of what the normal maps' levels
    // lost, which together widen the lobe once the roughness is read.
    float lostSlopes = slopesUnder(cone, hit.mSecondTurn, hit.mThirdTurn, hit.mShading, surface.mFootprint);

    // **Which side the ray met is the plane's answer, and the shading normal is not allowed to give a
    // different one.** Morrowind's vertex normals are authored coarsely enough to point clean through their
    // own triangle: a stretch of the floor in the Seyda Neen customs office interpolates to one aimed at the
    // ground, on a quad whose plane is level to a hundredth. Turned to face the *ray* that normal stays
    // pointing down, and a floor with a normal under it drops every lamp overhead on the cosine and sends its
    // bounce into itself.
    surface.mGeometric = faceforward(surface.mGeometric, direction, surface.mGeometric);
    const bool turned = dot(normal, surface.mGeometric) < 0.0;
    surface.mNormal = turned ? -normal : normal;
    surface.mSmooth = surface.mNormal;

    const GpuMaterial material = materialAt(instance.mMaterial);
    surface.mGround = material.mLayerCount > 0u;

    // **Fetched for every hit, and selected between without a branch.** A mesh that brought no colour holds
    // white, so the load answers neutrally rather than needing a case, and the two weights below are one or
    // nought (`MATERIAL_VERTEX_TINT` says why the mode does not survive the trip). Over half of Morrowind's
    // shapes and every piece of ground carry a colour, so a branch would be taken by most of the frame anyway.
    //
    // **A model is tinted by its colours' hue and the ground by its colours whole**: the ground's are the
    // level's painted shading and tint, which nothing traced stands in for.
    const TriangleColour colours = triangleColour(corner, hit.mBary);
    const vec3 vertexColour = surface.mGround ? colours.mColour : colours.mHue;
    const float tinted = float((material.mFlags & MATERIAL_VERTEX_TINT) != 0u);
    const float ambientTinted = float((material.mFlags & MATERIAL_VERTEX_AMBIENT) != 0u);
    const float glowing = float((material.mFlags & MATERIAL_VERTEX_GLOW) != 0u);

    surface.mEmissiveColour = mix(material.mEmissiveColour, colours.mColour, glowing);

    surface.mSheet = (mesh.mShape & MESH_SHEET) != 0u;
    surface.mTransmission = surface.mSheet && hasMask(material) ? SHEET_TRANSMISSION : 0.0;
    surface.mLampLit = (material.mFlags & MATERIAL_NO_LAMPS) == 0u;

    // The corner normals back out of what the traversal carried — the interpolated one and the other
    // two corners' difference from the first — turned as `mNormal` was, so no vertex is fetched
    // twice. A mesh with no normals carried noughts, which lift nothing.
    const float side = turned ? -1.0 : 1.0;
    vec3 corners[3];
    corners[0] = (hit.mShading - hit.mSecondTurn * hit.mBary.x - hit.mThirdTurn * hit.mBary.y) * side;
    corners[1] = corners[0] + hit.mSecondTurn * side;
    corners[2] = corners[0] + hit.mThirdTurn * side;
    surface.mLift = smoothLift(hit.mEdges, corners, hit.mBary) * float(!surface.mSheet);

    // What a sphere-mapped sheet's footprint reads of the corners, taken while they stand (`sheetAt`), and
    // only for a material that wears one, as the sheet is.
    SheetSpan span = SheetSpan(false, 0.0);
    if (holdsTexture(material.mEnvironment))
        span = sheetSpanOf(corners);

    vec2 uv[3];
    triangleUvs(corner, uv);

    // Where the hit lands on the material's own sheet, which the albedo, the opacity and the
    // emissive map all read at. A terrain layer has a transform of its own and makes its own.
    TexturePoint point = texturePoint(uv, hit.mBary, material.mTextureTransform, cone, surface.mFootprint, detailed);

    // **A normal map, read through the tangents the mesh carries**, in the frame `normals.glsl` builds: the
    // tangent unit, the bitangent `cross(N, T) * w`, and the interpolated normal, with no orthogonalisation
    // between them — which is what the maps were checked against.
    if (HAS_MAPS && detailed && holdsTexture(material.mNormal) && dot(hit.mTangent.xyz, hit.mTangent.xyz) > 0.0)
    {
        const vec3 tangent = normalize(hit.mTangent.xyz);
        const vec3 bitangent = cross(normal, tangent) * hit.mTangent.w;

        // **Shifted toward the eye by the map's height first**, where it carries one, so every read on this
        // sheet after it lands where the relief puts it: the map itself, the albedo, the opacity, and the
        // specular, emissive and dark maps. **Marched through the relief** where the scene has one that spans
        // more than one height, and read back where the trace marched it already.
        //
        // **The march its own branch, ahead of the shift's and not inside it**, so a tuple with no relief
        // compiles the shift's branch unchanged (`HAS_RELIEF`).
        if (HAS_RELIEF && (material.mFlags & MATERIAL_PARALLAX) != 0u)
        {
            const vec3 eye = vec3(dot(tangent, -direction), dot(bitangent, -direction), dot(normal, -direction));
            // A relief held flat is its shift (`reliefToward`).
            const vec2 bounds = hit.mMarches ? reliefBoundsOf(material.mRelief) : vec2(1.0, 0.0);
            const float height = sampleDiffuse(material.mNormal, point).a;
            const vec2 shown = hit.mRelief != RELIEF_UNMARCHED ? vec2(hit.mRelief, 0.0)
                : reliefSource == RELIEF_STORED ? reliefShiftOf(height)
                                                : reliefToward(reliefMapOf(material.mNormal), bounds, point, eye, height);
            surface.mRelief = shown.x;
            point.mAt += eye.xy * surface.mRelief;
            surface.mReliefSite = ReliefSite(
                material.mNormal, point.mAt, bounds, tangent, bitangent, normal, reliefLevelOf(material.mNormal, point));
            surface.mReliefDepth = reliefDepthOf(uv, material.mTextureTransform, cone, shown.y);
        }
        else if ((material.mFlags & MATERIAL_PARALLAX) != 0u)
        {
            const vec3 eye = vec3(dot(tangent, -direction), dot(bitangent, -direction), dot(normal, -direction));
            point.mAt += parallaxShift(eye, sampleDiffuse(material.mNormal, point).a);
        }

        const vec3 painted = sampleNormalMap(material.mNormal, point);
        lostSlopes += normalMapSlopes(material.mNormal, point);
        // Where the frame carries the normal nowhere, the plane stands in, where a normalised zero was a
        // NaN in every history that read it: a mesh with no normals carries noughts, and a file drawn
        // before its chain is made may hold a texel of (0.5, 0.5, 0.5), which a made map never does.
        const vec3 carried = tangent * painted.x + bitangent * painted.y + normal * painted.z;
        const vec3 mapped = dot(carried, carried) > 0.0 ? normalize(carried) : surface.mGeometric;

        surface.mNormal = facingRay(turned ? -mapped : mapped, surface.mSmooth, direction);
    }

    // **Ground summed from its stack** where the hit is a picture of ground near enough to be worth the
    // sharpness, or the chunk has no composite, and one fetch for everything else, an untextured surface
    // included: its diffuse is `TEXTURE_NEUTRAL`, whose one texel is white.
    //
    // **Near ground's bounce reads the composite**, which the layers' delit colours were baked into as
    // the stack sums them.
    const bool stacked = (material.mFlags & MATERIAL_STACKED) != 0u
        || ((material.mFlags & MATERIAL_STACKED_SEEN) != 0u && detailed)
        || (material.mLayerCount > 0u && !holdsTexture(material.mDiffuse));
    vec3 albedo = vec3(0.0);

    // Asked once: each is a load through a buffer reference, and three branches below ask it. A
    // stack's gloss is its layers', and the composite's beside it is not read under the stack.
    const bool specularMap = HAS_MAPS && !stacked && holdsTexture(material.mSpecular);

    // What a stack sums to, as `groundcomposite.comp` sums it into a chunk's composite and gloss
    // (`GroundSum`), and beside it what only a hit reads: the tangent-space normals, each weighted as the
    // layer's albedo is — the layers' own where they have a map and straight up where not — and what their
    // maps lost.
    GroundSum ground = noGround();
    float spread = 0.0;
    vec3 painted = vec3(0.0);
    bool relief = false;

    if (stacked)
    {
        // Each layer is a tiling texture masked by its own grid of weights, and the stack sums to
        // one where the masks were built to — the same sum the rasterizer reaches by drawing the
        // layers over each other with additive blending and one pass apiece.
        const vec2 chunkUv = acrossTriangle(uv[0], uv[1], uv[2], hit.mBary);

        // The frame every layer shares: the tangent the chunk's x, which is the world's, less its part along N,
        // and the bitangent `cross(N, T)`. A heightfield's normal leans up, so x is never along it.
        //
        // **Not `terrain.vert`'s frame**, whose bitangent is `cross(N, x)`, `sqrt(1 - Nx²)` long: on a slope
        // down x a replacer's map tilted the normal 0.71 as far across the slope as along it at 45 degrees.
        //
        // **Filled behind `HAS_MAPS` and not declared with it**: the optimizer the kernels are compared through
        // keeps arithmetic nothing reads, so a frame computed outside the branch would be in every vanilla
        // program and move its digest by a normalize.
        vec3 layerTangent = vec3(0.0);
        vec3 layerBitangent = vec3(0.0);
        vec3 layerEye = vec3(0.0);
        if (HAS_MAPS && detailed)
        {
            // **Down y where x is the normal itself**, a face of ground along ±x, where x less its
            // part along the normal is nought and its normalisation a NaN in every history that read it.
            // The axis is selected before the one projection, which carried twice held 256 bytes more
            // of the water's walk in shared memory on NVIDIA's.
            const vec3 across = abs(normal.x) < 1.0 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
            layerTangent = normalize(across - normal * dot(normal, across));
            layerBitangent = cross(normal, layerTangent);
            layerEye = vec3(dot(layerTangent, -direction), dot(layerBitangent, -direction), dot(normal, -direction));
        }

        // **The stack's relief marched once, through the heights its layers show summed** (`ReliefMaps`), and
        // every layer read where the march stood: the stack draws the sum of its layers' albedos, so the relief
        // it draws is the sum of their heights, and a layer with no height stands at the middle one. Behind
        // `HAS_RELIEF` and the chunk's own relief, so a stack with none shifts each layer as `terrain.frag` does.
        bool stackMarched = false;
        float stackToward = 0.0;
        if (HAS_RELIEF && HAS_MAPS && detailed && (material.mFlags & MATERIAL_PARALLAX) != 0u
            && hit.mRelief != RELIEF_UNMARCHED)
        {
            // Read back where the trace marched it, as a surface's is.
            stackMarched = true;
            stackToward = hit.mRelief;
        }
        else if (HAS_RELIEF && HAS_MAPS && detailed && (material.mFlags & MATERIAL_PARALLAX) != 0u)
        {
            ReliefMaps maps = ReliefMaps(0u, uvec4(NO_TEXTURE), vec4(0.0), 0.0);
            float total = 0.0;
            for (uint i = 0u; i < material.mLayerCount; ++i)
            {
                const GpuLayer layer = layerAt(material.mLayerOffset + i);
                const float showing = maskWeight(layer, chunkUv);
                if (showing <= 0.0)
                    continue;
                total += showing;
                const bool height = (layer.mFlags & LAYER_PARALLAX) != 0u && holdsTexture(layer.mNormal);
                if (height && maps.mCount < RELIEF_LAYERS)
                    takeReliefMap(maps, layer.mNormal, showing);
                else
                    maps.mFlat += showing;
            }

            if (maps.mCount > 0u && total > 0.0)
            {
                maps.mShares /= total;
                maps.mFlat /= total;

                // Every layer's sheet is the first's (`ReliefMaps`).
                const TexturePoint sheet = texturePoint(
                    uv, hit.mBary, layerAt(material.mLayerOffset).mDiffuseTransform, cone, surface.mFootprint, detailed);
                float height = 0.5 * maps.mFlat;
                [[unroll]] for (uint place = 0u; place < RELIEF_LAYERS; ++place)
                    if (place < maps.mCount)
                        height += maps.mShares[place] * sampleDiffuse(maps.mSlots[place], sheet).a;

                vec2 bounds = reliefBoundsOf(material.mRelief);
                if (maps.mFlat > 0.0)
                    bounds = vec2(min(bounds.x, 0.5), max(bounds.y, 0.5));
                if (!hit.mMarches)
                    bounds = vec2(1.0, 0.0);
                const vec2 shown = reliefSource == RELIEF_STORED ? reliefShiftOf(height)
                                                                 : reliefToward(maps, bounds, sheet, layerEye, height);
                stackMarched = true;
                stackToward = shown.x;
                surface.mRelief = shown.x;

                surface.mReliefDepth
                    = reliefDepthOf(uv, layerAt(material.mLayerOffset).mDiffuseTransform, cone, shown.y);
            }
        }

        for (uint i = 0u; i < material.mLayerCount; ++i)
        {
            const GpuLayer layer = layerAt(material.mLayerOffset + i);
            // **A layer the mask hides here is passed by**, its parallax, diffuse and normal fetches with
            // it: a select in their place cost the exteriors' trace 3 % at the median on a high-end NVIDIA
            // laptop GPU and their direct stage 2 to 6 %, the p99 higher with them, and the guild's
            // interior nothing, over three rounds each way of the default suite (2026-11-01).
            const float showing = maskWeight(layer, chunkUv);
            if (showing <= 0.0)
                continue;

            // Shifted as `terrain.frag` shifts the layer, before any read of it, by the height read
            // where the layer was.
            TexturePoint at = texturePoint(uv, hit.mBary, layer.mDiffuseTransform, cone, surface.mFootprint, detailed);
            const bool mapped = HAS_MAPS && detailed && holdsTexture(layer.mNormal);
            if (stackMarched)
                at.mAt += layerEye.xy * stackToward;
            else if (mapped && (layer.mFlags & LAYER_PARALLAX) != 0u)
                at.mAt += parallaxShift(layerEye, sampleDiffuse(layer.mNormal, at).a);

            const bool authored = HAS_MAPS && layerAuthored(layer, sceneTexels());
            const vec4 read = sampleDiffuse(layer.mDiffuse, at);
            const vec4 shown = layerTexel(read, authored);
            addLayer(ground, showing, shown, authored, HAS_MAPS);

            if (HAS_MAPS)
            {
                painted += showing * (mapped ? sampleNormalMap(layer.mNormal, at) : vec3(0.0, 0.0, 1.0));
                spread += mapped ? showing * normalMapSlopes(layer.mNormal, at) : 0.0;
                relief = relief || mapped;
            }
        }
        albedo = ground.mAlbedo;

        // **The layers' normals summed by their weights and then carried once**, through the layers' one
        // frame: summing before the frame is summing after it.
        if (HAS_MAPS && relief)
        {
            // The plane where the sum points nowhere, as for a single map above.
            const vec3 carried = layerTangent * painted.x + layerBitangent * painted.y + normal * painted.z;
            const vec3 mapped = dot(carried, carried) > 0.0 ? normalize(carried) : surface.mGeometric;

            surface.mNormal = facingRay(turned ? -mapped : mapped, surface.mSmooth, direction);
        }

        // And what the layers' maps lost, weighted as their roughness is.
        lostSlopes += ground.mWeights > 0.0 ? spread / ground.mWeights : 0.0;
    }
    else
        albedo = sampleDiffuse(material.mDiffuse, point).rgb;

    // The vertex colour *replaces* the material's tint where the content asked for it, which is
    // what `glColorMaterial(GL_AMBIENT_AND_DIFFUSE)` does and what `getDiffuseColor` reads in the
    // game's own shader. Multiplying the two together would tint a surface twice.
    const vec3 tint = mix(material.mDiffuseColour, vertexColour, tinted);
    surface.mAlbedo = albedo * tint;
    surface.mAmbientAlbedo = albedo * mix(material.mAmbientColour, vertexColour, ambientTinted);

    // **A specular map says the diffuse was authored as a base colour**, which is why it was read above
    // with nothing divided out of it: glTF's metal and roughness, the base colour split between what a
    // metal reflects and what a dielectric scatters.
    //
    // **The tint is on the lobe as well as the base**, so the two are darkened and coloured alike.
    //
    // **Ground reflects as a dielectric, over the share of it that reflects at all** — a stack from its
    // layers, a flattened chunk from the gloss baked beside its composite — and the tint darkens it for the
    // same reason.
    if (HAS_MAPS && ground.mReflecting > 0.0)
    {
        const vec2 gloss = groundGloss(ground);
        surface.mSpecular = vec3(DIELECTRIC_F0 * gloss.x) * tint;
        surface.mRoughness = gloss.y;
    }
    else if (specularMap && surface.mGround)
    {
        const vec2 gloss = sampleDiffuse(material.mSpecular, point).rg;
        surface.mSpecular = vec3(DIELECTRIC_F0 * gloss.x) * tint;
        surface.mRoughness = gloss.y;
    }
    else if (specularMap)
    {
        // **A `_spec` map is a metalness, a perceptual roughness and an occlusion**, a classic one converted
        // with its diffuse on the host (`ConversionPass`). The occlusion is of the relief no ray meets, since a
        // ray stops at the mesh; what real geometry occludes the traced bounce and `ambientReaching` already
        // find. The scattering in its alpha is not read: the BC1 maps most of the content ships cannot carry it.

        const vec4 painted = sampleDiffuse(material.mSpecular, point);
        const float metal = painted.x;
        surface.mSpecular = mix(vec3(DIELECTRIC_F0), albedo, metal) * tint;
        surface.mRoughness = painted.y;
        surface.mOcclusion = painted.z;
        surface.mAlbedo *= 1.0 - metal;
        surface.mAmbientAlbedo *= 1.0 - metal;
    }

    // **Widened by what the footprint averages away**, before anything reads the roughness: the lobe, the
    // energy it keeps and the cone a reflection leaves in.
    surface.mRoughness = widenedRoughness(surface.mRoughness, lostSlopes);

    // **Fetched again rather than kept from the albedo**, whose read keeps its colour alone: it is the
    // hottest sampler in the shader and an out-parameter there costs every opaque surface in the frame.
    // This is a fetch a pane of glass pays and nothing else does.
    const float opacity = surfaceOpacity(instance, material);
    const bool seenThrough = isSeenThrough(opacity, material);
    if (seenThrough)
        surface.mOpacity = sampledOpacity(opacity, material, point);

    // **The dark map multiplies the whole of it**, colour and alpha. At the unit the content bound
    // it at, on whichever set that unit reads: the Sixth House banners read their second set and
    // the durzog its first. White where there is none, so no surface asks twice.
    const vec4 dark = darkAt(material, mesh, corner, hit.mBary, point, cone, surface.mFootprint, detailed);
    surface.mAlbedo *= dark.rgb;
    surface.mAmbientAlbedo *= dark.rgb;

    // And the lobe, for the tint's reason: a dark map is light painted in.
    if (HAS_MAPS)
        surface.mSpecular *= dark.rgb;

    // The alpha only where an alpha is read at all: an opaque surface's is never written to the
    // frame, and the peel reads `mOpacity` as whether there is a layer to peel.
    if (seenThrough)
        surface.mOpacity *= dark.a;

    // At its own unit too: the draugrs' eye glow is bound on their second set.
    if (holdsTexture(material.mEmissive))
    {
        const uint unit = (material.mFlags >> MATERIAL_EMISSIVE_UNIT_SHIFT) & MATERIAL_UNIT_MASK;
        surface.mEmitted = EMISSIVE_INTENSITY
            * sampleDiffuse(material.mEmissive,
                pointAtUnit(unit, point, mesh, corner, hit.mBary, cone, surface.mFootprint, detailed))
                  .rgb;
    }

    // **Light of its own, or colour the light falls on**, as the player set the rasterizer's `preLightEnv`:
    // there it joins the diffuse colour past the dark map and before the lighting multiplies, so here it
    // joins the albedo at the sheet's own colour, held at one so a bounce returns no more than it met.
    const vec3 sheet = sheetAt(material, span, surface.mNormal, direction, cone, surface.mFootprint);
    const bool lit = frame.mLitEnvironmentMaps != 0u;
    surface.mEmitted += lit ? vec3(0.0) : sheet;
    surface.mAlbedo = lit ? min(surface.mAlbedo + sheet / SUNLIT_WHITE, vec3(1.0)) : surface.mAlbedo;
    surface.mAmbientAlbedo
        = lit ? min(surface.mAmbientAlbedo + sheet / SUNLIT_WHITE, vec3(1.0)) : surface.mAmbientAlbedo;

    return surface;
}

/// The ray that finds the surface a ray meets, to the reach, resolved (`resolveAsked`).
///
/// @param cone how wide the ray's cone starts — nothing for a primary ray, and for a reflection
///        whatever the pixel had already spread to at the water — and how much wider it gets per
///        unit travelled.
/// @param draws whether this ray draws the picture — `facingFor`. A reflection and the bed under a
///        waterline pixel do; a bounce carries light and does not.
RayAsk traceAsk(WorldRay ray, float tmin, Cone cone, uint mask, bool draws)
{
    // No blanket opaque flag: the per-instance bits the build set from each material are what decide
    // whether traversal stops to ask, and forcing opacity here would override them and put every leaf
    // back inside the card it was painted on.
    return rayOf(SceneQuery(ray.mFrom, tmin, ray.mAlong, frame.mReach, facingFor(draws), mask, MEET_BY_CHANCE,
        draws ? 1u : 0u, cone.mWidth, cone.mSpread, 0u, 0u));
}

/// The same numbers off a surface already resolved, where the stage resolves it for its own shading.
PlacedHit placedHitOf(Surface surface)
{
    return PlacedHit(surface.mHit, surface.mPosition, surface.mRounding, surface.mFootprint, surface.mDistance);
}

/// What `asked` met, resolved: its committed triangle, its cone opened over the distance it went, at the
/// detail it asked for, or no surface where it met nothing.
///
/// **Once for every surface a stage shades**, so the code that reads a material is in a stage once: at the
/// traversal that answered, the eye's own among them, or, for a walk that reads only whether and how far its
/// rays went (`stepWater`), once after the walk, from the ask made again (`waterPathAsk`).
Surface resolveAsked(SceneAnswer met, SceneQuery asked)
{
    Hit hit = noHit();
    if (met.mCommitted != 0u)
        hit = committedHit(met.mInstance, met.mPrimitive, met.mBary, met.mDistance, footprintOf(asked, met));

    return resolve(hit, asked.mFrom, asked.mAlong, asked.mDetailed != 0u, RELIEF_MARCHES);
}

#endif
