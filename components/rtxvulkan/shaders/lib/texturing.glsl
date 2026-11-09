#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TEXTURING_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TEXTURING_GLSL

#extension GL_EXT_control_flow_attributes : require

// Reading a texture at the level the ray's cone can resolve, and taking the painted-in lighting back out of
// it.

#include "look.h"
#include "scene.h"
#include "basis.glsl"
#include "bindings.glsl"
#include "geometry.glsl"
#include "ground.glsl"
#include "normaldecode.glsl"

/// What the hit's own triangle contributes to a mip level, before any texture is named.
///
/// **One statement of the half of `coneBase` that every point on a hit's triangle shares.** A terrain chunk
/// builds four or five points there, one a layer; each needs the same square root, the same divide by it
/// and the same dot against the ray. The triangle and the ray are one pair per hit, so this is worked out
/// once and handed over.
///
/// **It measures at no time either way**, because `glslc` inlines these and removes some of the repeat
/// itself.
struct SurfaceCone
{
    /// Twice the triangle's area in the world. Nought for a degenerate one, which reads level zero.
    float mArea;

    /// A surface seen edge-on covers more of itself per pixel, and the cone's footprint on it grows
    /// by the same factor. Floored, because a grazing hit sends it to infinity.
    float mFacing;

    /// How the second and the third corner's weights change across the triangle's plane, per world
    /// unit: what carries a step on the surface onto the texture, as `t1 dot(step, mToSecond) +
    /// t2 dot(step, mToThird)` for the texture's two edges `t1` and `t2`.
    vec3 mToSecond;
    vec3 mToThird;

    /// The ellipse a cone one unit wide makes on the surface: its short axis across the ray, one unit long,
    /// and its long axis along the ray's shadow on the plane, `1 / mFacing` long.
    ///
    /// **What `mFacing` folds into one level, kept apart.** An isotropic read has to take the long axis for
    /// both, so a surface seen at a grazing angle — most of the ground in most exterior frames — is blurred
    /// across the ray by the stretch along it. A read along both axes keeps the short one sharp
    /// (Akenine-Möller et al., *Improved Shader and Texture Level of Detail Using Ray Cones*, JCGT 10(1)
    /// 2021).
    vec3 mAcross;
    vec3 mAlong;
};

/// @param edges the triangle's two edges in the world. Their cross product's length is twice its
///        area; the texel area in `coneLod` is doubled the same way, so the two cancel in the ratio.
SurfaceCone surfaceConeAt(TriangleEdges edges, vec3 direction)
{
    const vec3 crossed = cross(edges.mFirst, edges.mSecond);
    const float area = length(crossed);
    if (!(area > 0.0))
        return SurfaceCone(0.0, 1.0, vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));

    const vec3 normal = crossed / area;
    const float facing = max(abs(dot(normal, direction)), 1e-3);

    // Where the ray's shadow on the plane is nothing, the ray meets the plane square and the ellipse
    // is a circle: any axis in the plane serves.
    const vec3 shadow = direction - normal * dot(direction, normal);
    const float shadowLength = length(shadow);
    const vec3 along = shadowLength > 1e-6 ? shadow / shadowLength : tangentTo(normal);

    const float squared = area * area;
    return SurfaceCone(area, facing, cross(edges.mSecond, crossed) / squared, cross(crossed, edges.mFirst) / squared,
        cross(normal, along), along / facing);
}

/// The variance of the shading normal's slopes over the ellipse a cone `footprint` wide makes on the
/// surface, both axes together, which is what `widenedRoughness` takes.
///
/// **Exact for the normal a hit shades with**, which is `m / |m|` for `m` the corner normals interpolated
/// by the corner weights.
///
/// @param secondTurn,thirdTurn,interpolated `Hit::mSecondTurn`, `mThirdTurn` and `mShading`: the
///        differences and `m`, in world space and not unit. Nought where the mesh has no normals,
///        which bends nothing.
float slopesUnder(SurfaceCone cone, vec3 secondTurn, vec3 thirdTurn, vec3 interpolated, float footprint)
{
    // Floored and not tested: a mesh with no normals has no differences either, and bends nothing.
    const float squared = max(dot(interpolated, interpolated), 1e-20);
    const vec3 normal = interpolated * inversesqrt(squared);
    const vec3 across
        = secondTurn * dot(cone.mToSecond, cone.mAcross) + thirdTurn * dot(cone.mToThird, cone.mAcross);
    const vec3 along = secondTurn * dot(cone.mToSecond, cone.mAlong) + thirdTurn * dot(cone.mToThird, cone.mAlong);
    const vec3 bentAcross = across - normal * dot(normal, across);
    const vec3 bentAlong = along - normal * dot(normal, along);

    return footprint * footprint / 16.0 * (dot(bentAcross, bentAcross) + dot(bentAlong, bentAlong)) / squared;
}

/// A base at or below which every texture reads its finest level, whatever its own resolution adds.
///
/// **Both a sentinel and a threshold, and nothing has to know which a given point is.** A ray with no cone
/// is given this so that `coneLod` loads no texel count for it, and a base that reaches it honestly would
/// clamp to the finest level anyway — the largest `0.5 * log2(w * h)` a `maxImageDimension2D` of 16384
/// allows is 14.
const float TEXTURE_FINEST_BASE = -64.0;

/// Every term of the level but the texture's own resolution: the texture-to-world area ratio of the
/// triangle, the cone's width where it landed, and the angle the surface presents.
///
/// **The ratio and not the coordinates it was measured from**, because a mesh's own texture coordinates are
/// not the only parameterisation a surface reads a sheet through: a sphere map is indexed by where the eye
/// is, and `spherePoint` measures the same ratio for it.
///
/// @param uvArea twice the triangle's area in the texture's own coordinates, against `cone.mArea`,
///        which is twice its area in the world. Doubled on both sides, so the two cancel.
/// @param coneWidth how wide the ray's cone is where it landed, or zero for a ray that carries no
///        cone at all — which is every shadow ray, and which reads the finest level.
float coneBaseOf(float uvArea, SurfaceCone cone, float coneWidth)
{
    if (!(coneWidth > 0.0) || !(cone.mArea > 0.0) || !(uvArea > 0.0))
        return TEXTURE_FINEST_BASE;

    // The frame's bias rides in the base, so every read through a `TexturePoint` — the albedo,
    // the mask, the emissive map, each ground layer — moves by the same levels. `mLevelBias`
    // says what it is for.
    return 0.5 * log2(uvArea / cone.mArea) + log2(coneWidth) - log2(cone.mFacing) + frame.mLevelBias;
}

/// The same for a triangle read through the mesh's own texture coordinates.
float coneBase(vec2 uv0, vec2 uv1, vec2 uv2, SurfaceCone cone, float coneWidth)
{
    const float uvArea = abs((uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv2.x - uv0.x) * (uv1.y - uv0.y));

    return coneBaseOf(uvArea, cone, coneWidth);
}

/// Where on a texture a hit lands, and how coarse a level the ray's cone can still tell apart there.
///
/// **One statement of a hit's place on a sheet, for every read made of that sheet.** A surface reads its
/// albedo, its opacity and its emissive map off one transform, so the three transformed corners are worked
/// out once per transform and handed to every read.
struct TexturePoint
{
    /// The hit, in the texture's own coordinates.
    vec2 mAt;

    /// The level any texture on this sheet is read at, before its own resolution is added.
    ///
    /// **The answer and not what it was made from.** The three transformed corners travelled to every
    /// sampler beside the surface's own cone so that each could take the same determinant again — eight
    /// floats through the call chain for one scalar that is the same for every map on the triangle, and a
    /// surface reads three of them.
    float mBase;

    /// The footprint's two axes in the texture's own coordinates, for `textureGrad`, and whether the
    /// read is along them: a surface the eye sees (`texturePoint`'s `anisotropic`). The frame's
    /// level bias is in their length.
    vec2 mAcross;
    vec2 mAlong;
    bool mAnisotropic;

    /// The level the read resolves, before the texture's own resolution is added: `mBase` where it
    /// is read at the long axis's level, and the area of what it averages where it is read along
    /// the footprint — `texturePoint` says how much that is.
    float mResolved;
};

/// @param transform mesh texture coordinates to this texture's, as `uv * xy + zw`.
/// @param anisotropic whether the texture is read along the footprint's two axes rather than at the
///        level of its long one: a surface the eye sees — `resolve`'s `detailed` — and the cutout
///        the eye's own traversal tests, so a hole and the leaf around it are read at one footprint.
///        **A literal at every call**, so the other reads compile as they were.
TexturePoint texturePoint(vec2 uv[3], vec2 bary, vec4 transform, SurfaceCone cone, float coneWidth, bool anisotropic)
{
    const vec2 corner0 = uv[0] * transform.xy + transform.zw;
    const vec2 corner1 = uv[1] * transform.xy + transform.zw;
    const vec2 corner2 = uv[2] * transform.xy + transform.zw;

    TexturePoint point;
    point.mAt = acrossTriangle(corner0, corner1, corner2, bary);
    point.mBase = coneBase(corner0, corner1, corner2, cone, coneWidth);
    point.mAcross = vec2(0.0);
    point.mAlong = vec2(0.0);

    // **One path whatever the lane has**: a ray with no cone, or a triangle with no area on the
    // surface or on the texture, comes out with no gradient at all, which is level nought — what
    // `coneBase` gives it as the finest.
    point.mAnisotropic = anisotropic;
    point.mResolved = point.mBase;
    if (anisotropic)
    {
        const vec2 second = corner1 - corner0;
        const vec2 third = corner2 - corner0;
        const float width = coneWidth * exp2(frame.mLevelBias);

        point.mAcross = (second * dot(cone.mAcross, cone.mToSecond) + third * dot(cone.mAcross, cone.mToThird)) * width;
        point.mAlong = (second * dot(cone.mAlong, cone.mToSecond) + third * dot(cone.mAlong, cone.mToThird)) * width;

        // **What the filter averages is the long axis by the level it reads at**: up to `mAnisotropy` taps
        // along the long axis, each as wide as the long axis over the taps or the short axis, whichever is
        // wider.
        const float major = max(length(point.mAcross), length(point.mAlong));
        const float minor = min(length(point.mAcross), length(point.mAlong));
        const float covered = max(minor, major / frame.mAnisotropy);
        point.mResolved = major > 0.0 ? 0.5 * log2(major * covered) : TEXTURE_FINEST_BASE;
    }

    return point;
}

/// Where a sphere-mapped sheet is read, and how coarse a level the ray's cone can tell apart there.
///
/// **The sheet is parameterised by the reflection, so its footprint is the normal's and not the mesh's.**
/// The two share nothing but the ray, so a `TexturePoint` built for the mesh's own coordinates is wrong in
/// either direction and by any amount: it blurs a flat blade, whose sheet coordinate does not move at all,
/// and it aliases the streaks on a curve that sweeps them.
///
/// **The ratio is closed form and not an estimate.** The sphere map `r.xy / (2 sqrt(2 + 2 r.z))` is
/// Lambert's azimuthal equal-area projection over four, so it carries solid angle to sheet area at exactly
/// one sixteenth, everywhere. Reflecting about a normal carries solid angle by `4 cos`, for the angle
/// between the ray and that normal — the shading normal the sheet is reflected about, and not the plane's
/// `SurfaceCone::mFacing`, which is how wide the ray's own footprint lies and a different question.
///
/// **Solid angle is rotation invariant, so the normals never leave the mesh's own space.** Bringing them
/// across would want the object-to-world matrix, and `Hit` refuses to carry one: twelve floats on every ray
/// in the frame, for a sheet nearly no material has. A placement scaled unevenly would move the span, which
/// is the assumption the whole tree already makes of a normal — `Hit::mShading` comes through the same
/// matrix rather than its inverse transpose.
///
/// **What this leaves out is the ray's own turn across the triangle**, which moves the reflection as much
/// as the normal does.
///
/// @param normal the triangle's three vertex normals, as `triangleNormals` hands them: the mesh's
///        own space, and not yet unit.
/// @param shading where the hit's own normal points, in the caller's basis and unit.
/// @param direction the ray, in the caller's basis and unit.
TexturePoint spherePoint(vec3 normal[3], vec3 shading, vec3 direction, SurfaceCone cone, float coneWidth)
{
    const vec3 r = reflect(direction, shading);
    const float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
    const vec2 at = r.xy / m + 0.5;

    // **Unit, because what the three tips span is a solid angle on the sphere** and a stored normal is not
    // one.
    const float shortest
        = min(min(dot(normal[0], normal[0]), dot(normal[1], normal[1])), dot(normal[2], normal[2]));
    if (!(shortest > 1e-8))
        return TexturePoint(at, TEXTURE_FINEST_BASE, vec2(0.0), vec2(0.0), false, TEXTURE_FINEST_BASE);

    const vec3 tip = normalize(normal[0]);
    const vec3 spread = cross(normalize(normal[1]) - tip, normalize(normal[2]) - tip);

    // Doubled, as `coneBaseOf` takes it: the tips' own triangle is half this cross, the solid angle
    // the reflections span is `4 cos` of that, and a sixteenth of it is sheet area.
    const float sheetArea = 0.25 * abs(dot(direction, shading)) * length(spread);

    const float base = coneBaseOf(sheetArea, cone, coneWidth);
    return TexturePoint(at, base, vec2(0.0), vec2(0.0), false, base);
}

/// Which mip one texture on that sheet should be read from.
///
/// **The texture's own term is a load and not a header read.** `textureSize` asked the driver for the
/// slot's extent on every sample; the texel count is one word per slot for the life of the slot, and the
/// array's owner writes it beside the descriptor — `GpuTables::mTextureTexels`. The logarithm stays here,
/// over the same integer, so no level moves.
///
/// **The early answer is a read and not an arithmetic saving.** `passageAsk` says why a shadow ray takes
/// level zero, and what it saves is the load this makes; on a shadow ray the width is a literal nought and
/// the test folds.
float coneLod(uint slot, TexturePoint point)
{
    if (point.mBase <= TEXTURE_FINEST_BASE)
        return 0.0;

    return point.mBase + 0.5 * log2(float(textureTexelsAt(slot)));
}

/// How far a normal map's `height` shifts a texture point toward the eye, `eye` being the unit direction to
/// the eye in the map's tangent frame: `PARALLAX_SCALE`'s shift, the rasterizer's own.
///
/// **With offset limiting, as the rasterizer's form has it**: linear in the eye's lean, where dividing by
/// the eye's height would slide a texture off a surface met at grazing.
vec2 parallaxShift(vec3 eye, float height)
{
    return eye.xy * (height * PARALLAX_SCALE + PARALLAX_BIAS);
}

/// The diffuse texel a hit landed on, read at the level its cone can resolve.
vec4 sampleDiffuse(uint slot, TexturePoint point)
{
    if (point.mAnisotropic)
        return textureGrad(TEXTURE_ALONG(sceneTexels(), slot), point.mAt, point.mAcross, point.mAlong);

    return textureLod(TEXTURE(sceneTexels(), slot), point.mAt, coneLod(slot, point));
}

/// A tangent-space normal off a normal map, read at the level its cone can resolve and decoded as
/// `decodeNormal` says, not unit — the frame it is carried through is normalised after.
vec3 sampleNormalMap(uint slot, TexturePoint point)
{
    return decodeNormal(sampleDiffuse(slot, point).rgb);
}

/// The slope variance a normal map's level has lost under the cone that reads it: its spread
/// (`NormalSpreadPass`), at the level `sampleNormalMap`'s read resolves (`TexturePoint::mResolved`), which
/// is what `widenedRoughness` takes.
float normalMapSlopes(uint slot, TexturePoint point)
{
    const float level = point.mResolved <= TEXTURE_FINEST_BASE
        ? 0.0
        : point.mResolved + 0.5 * log2(float(textureTexelsAt(slot)));
    const float lost = textureLod(COMPANION(sceneTexels(), slot), point.mAt, max(level - 1.0, 0.0)).r;
    const float roughness = lost * clamp(level, 0.0, 1.0);

    return roughness * roughness * roughness * roughness;
}


/// How much of a terrain layer shows at `uv`, from the scene's grid of weights — `maskWeightIn`.
float maskWeight(GpuLayer layer, vec2 uv)
{
    return maskWeightIn(layer, uv, MaskTable(frame.mTables.mMasks));
}

/// The heights a material's relief spans (`GpuMaterial::mRelief`), as its normal map's alpha reads them:
/// the lowest in `x` and the highest in `y`.
vec2 reliefBoundsOf(uint packed)
{
    return vec2(float(packed & 0xffu), float((packed >> 8u) & 0xffu)) / 255.0;
}

/// The level of normal map `slot` the reads at `point` resolve, nought at the finest: what a march through
/// its relief counts its texels at (`reliefToward`, `reliefLit`).
float reliefLevelOf(uint slot, TexturePoint point)
{
    return point.mResolved <= TEXTURE_FINEST_BASE
        ? 0.0
        : max(point.mResolved + 0.5 * log2(float(textureTexelsAt(slot))), 0.0);
}

/// The most normal maps a march sums the heights of (`ReliefMaps`): the layers of a ground stack that show
/// at a hit, past which the rest stand at the middle height. Ground blends two or three at a point.
const uint RELIEF_LAYERS = 4u;

/// The heights a march goes through (`reliefToward`): a surface's one normal map, or a stack's layers that
/// show at the hit, each by the share of the stack it shows, beside the share of the layers with no height,
/// which stand at the middle height, on the triangle. The shares sum to one. **One sheet for all of them**:
/// every layer of a chunk is laid by one transform (`Rtx::GroundReader`), so one point names a texel of each.
///
/// **Vectors and not arrays, and every index a constant**: an array indexed by a count the hit decides is
/// local memory on NVIDIA's compiler, which spilled the direct stage and the landing's by 32 bytes a lane.
struct ReliefMaps
{
    uint mCount;
    uvec4 mSlots;
    vec4 mShares;
    float mFlat;
};

/// A surface's one normal map, `slot`, whole.
ReliefMaps reliefMapOf(uint slot)
{
    return ReliefMaps(1u, uvec4(slot, NO_TEXTURE, NO_TEXTURE, NO_TEXTURE), vec4(1.0, 0.0, 0.0, 0.0), 0.0);
}

/// `maps` with `slot` taken in at `share`, where it has room: each place compared against the count, so no
/// index is the count's.
void takeReliefMap(inout ReliefMaps maps, uint slot, float share)
{
    [[unroll]] for (uint place = 0u; place < RELIEF_LAYERS; ++place)
        if (place == maps.mCount)
        {
            maps.mSlots[place] = slot;
            maps.mShares[place] = share;
        }
    ++maps.mCount;
}

/// The height `maps` sum to at `at`, read at `level`.
float reliefHeightAt(ReliefMaps maps, vec2 at, float level)
{
    float height = 0.5 * maps.mFlat;
    [[unroll]] for (uint place = 0u; place < RELIEF_LAYERS; ++place)
        if (place < maps.mCount)
            height += maps.mShares[place] * textureLod(TEXTURE(sceneTexels(), maps.mSlots[place]), at, level).a;
    return height;
}

/// How far toward the eye a hit's relief shows its texture, in the texture's own units along the eye's
/// lean, in `x`: the texel the eye sees stands at `point.mAt + eye.xy * reliefToward(...).x`. And in `y` the
/// march's own share of it, past the shift's, which stands a point along the eye's ray and nought where the
/// march took no step: the shift moves the texture and stands it at no depth.
///
/// **The relief OpenMW states, marched** (Tatarchuk 2006): a height `h` stands `h * PARALLAX_SCALE +
/// PARALLAX_BIAS` over the triangle in the map's tangent frame, measured in the texture's units, so the
/// mean height the replacers paint stands on it. The eye's ray enters the relief at its highest height and
/// is walked down in even steps to its lowest; the first step under the height ends it, and the root is
/// found in float between that step and the one before, on the line through their two heights, which
/// joins the filtered bytes without the stairs a step would leave.
///
/// **The steps follow the ray's cone.** As many as the texels the ray crosses sideways inside the bounds,
/// at the level the hit's reads resolve, up to `RELIEF_STEPS`; past it a coarser level, so a step stays
/// one texel of the level it reads. **Under one texel, the shift**, `parallaxShift`'s, which is what the
/// march comes to where nothing it crosses tells one texel from another: a far surface keeps the picture
/// and the cost it had. Between one texel and two the two are mixed, so no ring stands where the march
/// begins (Tatarchuk's threshold blend). A face the eye meets from behind its frame takes the shift.
///
/// **Held to one side of the texture**: the eye's rise over the sheet is taken at least as high as a
/// crossing of the whole relief over one side needs, since past it the texture repeats and the march
/// would read a tile it does not stand over.
///
/// @param maps the normal maps whose alpha is the height, the first of which the march counts its texels
///        at.
/// @param bounds the lowest and the highest height they hold (`reliefBoundsOf`).
/// @param eye the direction to the eye in the maps' tangent frame, as `parallaxShift` takes it.
/// @param height their height at the hit, read as the shift reads it.
vec2 reliefToward(ReliefMaps maps, vec2 bounds, TexturePoint point, vec3 eye, float height)
{
    const uint slot = maps.mSlots[0];
    const float shift = height * PARALLAX_SCALE + PARALLAX_BIAS;
    const float top = bounds.y * PARALLAX_SCALE + PARALLAX_BIAS;
    const float bottom = bounds.x * PARALLAX_SCALE + PARALLAX_BIAS;
    const float depth = top - bottom;
    if (!(eye.z > 0.0) || !(depth > 0.0))
        return vec2(shift, 0.0);

    const float lean = length(eye.xy);
    const float rise = max(eye.z, lean * depth);
    const float texels = float(textureTexelsAt(slot));
    const float level = reliefLevelOf(slot, point);
    const float crossed = lean / rise * depth * sqrt(texels) * exp2(-level);
    if (!(crossed > 1.0))
        return vec2(shift, 0.0);

    const float steps = min(ceil(crossed), RELIEF_STEPS);
    const float read = level + max(log2(crossed / RELIEF_STEPS), 0.0);
    const vec2 along = eye.xy / rise;
    const float step = depth / steps;

    // **What a step stands under, in float**: the first that is under the height ends the walk, and the
    // one before it was over, by `over`. A walk that ends under none stands on the lowest height.
    float met = bottom;
    float over = 0.0;
    const uint count = uint(steps);
    for (uint at = 0u; at <= count; ++at)
    {
        const float elevation = top - float(at) * step;
        const float stands = reliefHeightAt(maps, point.mAt + along * elevation, read) * PARALLAX_SCALE + PARALLAX_BIAS;
        const float gap = elevation - stands;
        if (!(gap > 0.0))
        {
            met = at == 0u ? elevation : elevation - step * gap / (over - gap);
            break;
        }
        over = gap;
    }

    const float marched = clamp(crossed - 1.0, 0.0, 1.0);
    return vec2(mix(shift, met / rise, marched), marched * (met / rise));
}

/// How much farther along the ray than the triangle a march's texel stands, in the world (`Surface::mReliefDepth`):
/// `marched`, the march's own share (`reliefToward`'s `y`) in the units of the sheet `transform` lays, carried by
/// the triangle's world length of one, the square root of its area in the world over its area on the sheet, so a
/// texture stretched unevenly takes the mean of its two stretches. Nought where the sheet has no area.
float reliefDepthOf(vec2 uv[3], vec4 transform, SurfaceCone cone, float marched)
{
    const vec2 across = (uv[1] - uv[0]) * transform.xy;
    const vec2 down = (uv[2] - uv[0]) * transform.xy;
    const float sheetArea = abs(across.x * down.y - down.x * across.y);
    return sheetArea > 0.0 ? -marched * sqrt(cone.mArea / sheetArea) : 0.0;
}

/// Where a hit's relief showed the eye its texel, for a march from there toward a light (`reliefLit`): the
/// normal map, or `NO_TEXTURE` where the surface has no relief; the texel, past the march; the heights the
/// map spans (`reliefBoundsOf`); the map's tangent frame, as `reliefToward` was handed the eye in it; and the
/// level its reads resolve (`reliefLevelOf`).
struct ReliefSite
{
    uint mMap;
    vec2 mAt;
    vec2 mBounds;
    vec3 mTangent;
    vec3 mBitangent;
    vec3 mNormal;
    float mLevel;
};

ReliefSite noReliefSite()
{
    return ReliefSite(NO_TEXTURE, vec2(0.0), vec2(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), 0.0);
}

/// How much of a light `toLight` lights the texel `site` showed the eye, past the relief between: a march
/// from the height that texel stands at up along the light, through the map's heights to its highest.
///
/// **Soft, by the light's own size** (Tatarchuk 2006's approximate soft shadow, here with the light's
/// width): at each step a height that stands over the light's ray is weighed against how wide the light's
/// cone has opened there, `spread` times the distance from the texel, and takes the light away in a line,
/// all of it where the height stands the cone's whole width over the ray. **Only what stands over the ray**,
/// as Remix's `pomSampleVisibility` counts it: a height under the ray takes nothing, so flat ground lights
/// itself whole. **A texel of the level the march reads is the narrowest width**, since the heights are
/// filtered over it and a shadow sharper than a texel would be the filter's stairs. The least of the steps
/// is the answer.
///
/// **The march's own rules, the eye's level among them**: as many steps as the texels the light's ray crosses
/// sideways, up to `RELIEF_STEPS`, and none where it crosses under one, which lights the texel whole. A light
/// under the map's plane lights it whole as well, where the cosine already takes the light away.
///
/// @param toLight unit, toward the light.
/// @param spread the sine of half the angle the light subtends: the sky source's limb, a lamp's radius over
///        its distance.
float reliefLit(ReliefSite site, vec3 toLight, float spread)
{
    const vec3 light = vec3(dot(site.mTangent, toLight), dot(site.mBitangent, toLight), dot(site.mNormal, toLight));
    const float top = site.mBounds.y * PARALLAX_SCALE + PARALLAX_BIAS;
    const float start = textureLod(TEXTURE(sceneTexels(), site.mMap), site.mAt, site.mLevel).a * PARALLAX_SCALE
        + PARALLAX_BIAS;
    const float depth = top - start;
    if (!(light.z > 0.0) || !(depth > 0.0))
        return 1.0;

    const float lean = length(light.xy);
    const float rise = max(light.z, lean * depth);
    const float side = sqrt(float(textureTexelsAt(site.mMap)));
    const float crossed = lean / rise * depth * side * exp2(-site.mLevel);
    if (!(crossed > 1.0))
        return 1.0;

    const float steps = min(ceil(crossed), RELIEF_STEPS);
    const float read = site.mLevel + max(log2(crossed / RELIEF_STEPS), 0.0);
    const vec2 along = light.xy / rise;
    const float step = depth / steps;
    const float span = sqrt(lean * lean + rise * rise);
    const float texel = exp2(read) / side;

    float lit = 1.0;
    const uint count = uint(steps);
    for (uint at = 1u; at <= count; ++at)
    {
        const float risen = float(at) * step;
        const float stands = textureLod(TEXTURE(sceneTexels(), site.mMap), site.mAt + along * risen, read).a
                * PARALLAX_SCALE
            + PARALLAX_BIAS;
        const float over = (stands - (start + risen)) * lean / span;
        const float width = max(risen * span / rise * spread, texel);
        lit = min(lit, clamp(1.0 - over / (2.0 * width), 0.0, 1.0));
    }

    return lit;
}

#endif
