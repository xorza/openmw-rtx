#ifndef OPENMW_COMPONENTS_RTX_SHADERS_BOUNCEREUSE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_BOUNCEREUSE_H

#include "hosttypes.h"
#include "portable.h"

// What the bounce's reuse keeps for each traced pixel, and the numbers it runs by: ReSTIR GI
// (Ouyang et al. 2021) over the one diffuse bounce the trace draws. Included verbatim by both
// sides, for the reason `visibility.h` is.
//
// **Spatiotemporal reservoir resampling of the bounce's far end.** The trace's own bounce is each
// pixel's candidate: where its ray landed, the light leaving that point toward the pixel, and one
// over the chance of the direction. A pass after the trace merges last frame's reservoir into it,
// and a launch after that merges a few neighbours' and shades the one sample kept, so a pixel that
// met a dark wall this frame can still show the lit patch it or a neighbour found before.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Which reuse a frame runs — `Rtx::BounceReuse`, in its order.
    const uint BOUNCE_REUSE_OFF = 0u;
    const uint BOUNCE_REUSE_OWN = 1u;
    const uint BOUNCE_REUSE_TEMPORAL = 2u;
    const uint BOUNCE_REUSE_SPATIOTEMPORAL = 3u;

    /// One kept sample of a pixel's bounce, and what it is worth.
    ///
    /// **An offset from the visible point and not a world position**, because Morrowind's positions
    /// run to six figures and the device never subtracts two of them (`reproject.glsl`): every
    /// direction and distance the reuse reads is a difference of offsets from the eye. A sample at
    /// infinity — the sky a bounce escaped to — holds a unit direction instead, and
    /// `BOUNCE_STATE_SKY` says so.
    struct GpuBounceReservoir
    {
        /// The sample point less the visible point that found it, in world units.
        vec3 mOffset;

        /// The sample point's normal, `octahedralCode` at sixteen bits a coordinate: what the
        /// Jacobian of a shift to another visible point reads. Nought for the sky.
        uint mNormal;

        /// What leaves the sample point toward the visible point, whole, and the share of it that
        /// is the fill (`Arriving`), each in shared-exponent `RGB9E5`.
        uint mRadiance;
        uint mFill;

        /// The unbiased contribution weight `W`: one over the chance of the direction for a fresh
        /// candidate, and what resampling makes of it after.
        float mWeight;

        /// The confidence in its low byte, the age in frames in the next, and `BOUNCE_STATE_*`.
        uint mState;
    };

    /// What `GpuBounceReservoir::mState` holds past its two counts.
    const uint BOUNCE_STATE_SKY = 0x10000u;

    /// The visible point a pixel's bounce left from, and what its diffuse half makes of a direction:
    /// what reuse needs of a pixel to shade another's sample there.
    ///
    /// **Its own, and not `CHANNEL_SURFACE`**: at a waterline the channel holds the water's
    /// distance and a mixed normal, and the bounce left from the bed; and a sheet's transmission and
    /// a lobe's reflectance are in no channel. **And where it stands, and not how far**: last
    /// frame's point is read against this frame's eye, which the camera's step carries it to
    /// (`VisibilityConstants::mCameraMotion`), where a distance would need last frame's ray.
    struct GpuBounceOrigin
    {
        /// The visible point less the eye of the frame that found it, in world units.
        vec3 mOffset;

        /// The shading normal and the triangle's, `octahedralCode` at sixteen bits a coordinate.
        uint mNormal;
        uint mPlane;

        /// The lobe's reflectance at normal incidence, a byte a channel. The edge's follows from it
        /// (`specularEdge`).
        uint mReflectance;

        /// The sheet's transmission in the low sixteen bits, and `BOUNCE_ORIGIN_*`.
        uint mSheet;
    };

    /// What `GpuBounceOrigin::mSheet` holds past the transmission: whether the surface has a lobe,
    /// which takes `1 - F` off its diffuse half; whether `mPlane` holds a triangle; and whether the
    /// pixel left a bounce at all.
    const uint BOUNCE_ORIGIN_GLOSSY = 0x10000u;
    const uint BOUNCE_ORIGIN_PLANED = 0x20000u;
    const uint BOUNCE_ORIGIN_KEPT = 0x40000u;

    /// Threads along each edge of the temporal pass's workgroup.
    const uint BOUNCE_TEMPORAL_WORKGROUP = 8;

    /// How many candidates a reservoir may stand for. **The cap is what keeps temporal reuse from
    /// holding one sample for ever**, and spatial reuse from growing it past every new candidate:
    /// without one the result converges to a wrong value (Wyman et al. 2023, *Confidence
    /// weights*, which advise 5 to 30 and starting at 20).
    const uint BOUNCE_CONFIDENCE_CAP = 20u;

    /// How many frames a kept sample may stay before it is let go, whatever its weight: the bound
    /// on how stale a light it holds can be. RTXDI's default.
    const uint BOUNCE_AGE_CAP = 30u;

    /// The neighbours the spatial reuse reads, and the disc they are drawn from, in traced pixels.
    /// RTXDI's defaults.
    const uint BOUNCE_NEIGHBOURS = 2u;
    const float BOUNCE_RADIUS = 32.0f;

    /// How nearly two visible points must face alike, and how near their distances, for one to
    /// reuse the other's samples: RTXDI's defaults, the cosine and a share of the distance.
    const float BOUNCE_FACING = 0.6f;
    const float BOUNCE_DEPTH = 0.1f;

    /// How far from one the Jacobian of a shift may stand before the shift is refused. A
    /// reconnection of a very different length puts a ratio of squares into the weight, and the
    /// variance it adds is larger than what the sample brings (Wyman et al. 2023, §6.4).
    const float BOUNCE_JACOBIAN_LIMIT = 10.0f;

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(GpuBounceReservoir) == 32, "GpuBounceReservoir must be scalar-packed on every side");
    static_assert(sizeof(GpuBounceOrigin) == 28, "GpuBounceOrigin must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
