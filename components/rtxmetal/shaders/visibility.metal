#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace raytracing;

#include "scene.h"
#include "visibility.h"

// Primary visibility. One ray per pixel against the instance structure, shaded by the albedo of
// whatever it hit — the same question `visibility.comp` answers, in the language of the other
// backend. The structures both read are the same file.

/// The tables a hit reads, as one argument rather than fifteen.
///
/// Metal has no program-scope resources, so a kernel's inputs arrive as arguments and anything that
/// reads them takes this. The Vulkan shader is being brought to the same shape for the same reason
/// (docs/rtx/backends.md §4).
struct Scene
{
    device const GpuInstance* mInstances;
    device const GpuMaterial* mMaterials;
};

/// Display encoding, matching `encodeSrgb` in the Vulkan shader term for term.
static float3 encodeSrgb(float3 linear)
{
    const float3 low = linear * 12.92f;
    const float3 high = 1.055f * pow(max(linear, float3(0.0f)), float3(1.0f / 2.4f)) - 0.055f;

    return clamp(select(high, low, linear <= float3(0.0031308f)), 0.0f, 1.0f);
}

/// The sky's own colour along a direction: the game's horizon fading to its zenith.
///
/// **The sun is not in it.** The Vulkan shader answers a miss with `skyRadiance`, which draws the
/// disc on top of this and is what puts a sun in a reflection and a glitter path on water; that
/// arrives with the cone width it is widened by.
static float3 skyGlow(constant VisibilityConstants& camera, float3 direction)
{
    return mix(float3(camera.mSkyHorizon), float3(camera.mSkyZenith), clamp(direction.z, 0.0f, 1.0f));
}

kernel void visibility(instance_acceleration_structure scene [[buffer(0)]],
    constant VisibilityConstants& camera [[buffer(1)]], constant Scene& tables [[buffer(2)]],
    device atomic_uint* hits [[buffer(3)]], texture2d<float, access::write> target [[texture(0)]],
    uint2 pixel [[thread_position_in_grid]])
{
    if (pixel.x >= camera.mWidth || pixel.y >= camera.mHeight)
        return;

    // Pixel centres, and y downwards in the image against z upwards in the world.
    const float2 uv = (float2(pixel) + 0.5f) / float2(camera.mWidth, camera.mHeight) * 2.0f - 1.0f;
    const float3 direction
        = normalize(float3(camera.mForward) + float3(camera.mRight) * uv.x - float3(camera.mUp) * uv.y);

    ray probe;
    probe.origin = float3(camera.mOrigin);
    probe.direction = direction;
    probe.min_distance = 0.0f;
    probe.max_distance = camera.mFar;

    intersection_query<instancing, triangle_data> query;
    query.reset(probe, scene, MASK_SOLID | MASK_WATER);

    // Every instance this backend builds is opaque so far, so traversal never stops to ask and this
    // runs to completion in one step. The candidate loop the cutouts need arrives with them.
    while (query.next())
    {
    }

    const bool hit = query.get_committed_intersection_type() == intersection_type::triangle;

    float3 colour;
    if (hit)
    {
        // The instance's index is its position in the array the structure was built from, which is
        // the order `makeInstanceRecords` produced and so the row the tables are keyed by.
        const GpuInstance instance = tables.mInstances[query.get_committed_instance_id()];
        const GpuMaterial material = tables.mMaterials[instance.mMaterial];

        // Untextured surfaces are mid-grey rather than black, so a missing texture reads as missing
        // rather than as shadow. Textured ones arrive with the bindless array.
        colour = float3(0.5f) * float4(material.mDiffuseColour).xyz;

        atomic_fetch_add_explicit(hits, 1u, memory_order_relaxed);
    }
    else
        colour = skyGlow(camera, direction);

    target.write(float4(encodeSrgb(colour), 1.0f), pixel);
}
