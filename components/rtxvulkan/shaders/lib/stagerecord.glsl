#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_STAGERECORD_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_STAGERECORD_GLSL

// What the trace hands its later stages of a pixel (`STAGE_RECORD`), with no image bound: the stages
// read and write it through `stage.glsl`, and the passes that compose the frame read its masks'
// halves (`resolve.glsl`).

/// Which solid a pixel's stages light, as the trace found it: none, the eye's own behind its layers,
/// or what the pixel's field's water path found (`WaterRun`), cast again from the water the eye's ray
/// met.
const uint STAGE_NO_SOLID = 0u;
const uint STAGE_EYE_SOLID = 1u;
const uint STAGE_WATER_PATH = 2u;

/// Where a stage record's flags word holds whether a column of water on the pixel's path is one the
/// media stage marches (`EyeRun::mMarches`).
const uint STAGE_MEDIA = 1u << 3u;

/// And whether a see-through layer stands in front of the pixel's surface, whose peel the layers'
/// stage walks again: a pixel of a tile its list holds that no layer stands in is left as it is.
const uint STAGE_LAYERED = 1u << 4u;

/// And whether the solid is resolved at the detail of a picture (`SceneQuery::mDetailed`): the eye's
/// own, and a water path's that draws.
const uint STAGE_DETAILED = 1u << 5u;

/// And, once the bounce's stage has run (`bounce.comp`), whether the lobe drew the bounce: what it
/// brings back is the lobe's, and its landing is resolved at a picture's detail and shaded as seen
/// (`BounceDraw::mSpecular`).
const uint STAGE_BOUNCE_LOBE = 1u << 6u;

/// What the trace hands its later stages of a pixel, `STAGE_RECORD`: which solid, through which eye
/// and from where along the eye's last ray, which the media stage casts again (`eyeAsk`,
/// `WaterRun`); whether the media stage marches a column of its path, whether a layer stands in
/// front, and the detail the solid is resolved at (`storedSolidOf`); and the two halves of the pane
/// mask the whole pixel's light divides — the layers' own light and how far apart they move from
/// the surface — beside the water's share, which the trace measured whole; and what the bounce's
/// stage adds for its landing's.
struct StageRecord
{
    uint mKind;
    uint mArms;
    bool mMedia;
    bool mLayered;
    bool mDetailed;
    bool mBounceLobe;
    float mTmin;
    float mPaneOwn;
    float mPaneApart;
    float mWaterMisMoved;
};

/// Four words: the kind, the eye and the flags in the first, the start's bits in the second, the
/// layers' light's in the third, and the two shares as halves in the fourth — the upscaler's masks
/// hold them in bytes.
uvec4 packStageRecord(StageRecord record)
{
    return uvec4(record.mKind | record.mArms << 2u | (record.mMedia ? STAGE_MEDIA : 0u)
            | (record.mLayered ? STAGE_LAYERED : 0u) | (record.mDetailed ? STAGE_DETAILED : 0u)
            | (record.mBounceLobe ? STAGE_BOUNCE_LOBE : 0u),
        floatBitsToUint(record.mTmin), floatBitsToUint(record.mPaneOwn),
        packHalf2x16(vec2(record.mPaneApart, record.mWaterMisMoved)));
}

StageRecord unpackStageRecord(uvec4 words)
{
    const vec2 shares = unpackHalf2x16(words.w);
    return StageRecord(words.x & 3u, (words.x >> 2u) & 1u, (words.x & STAGE_MEDIA) != 0u,
        (words.x & STAGE_LAYERED) != 0u, (words.x & STAGE_DETAILED) != 0u, (words.x & STAGE_BOUNCE_LOBE) != 0u,
        uintBitsToFloat(words.y), uintBitsToFloat(words.z), shares.x, shares.y);
}

#endif
