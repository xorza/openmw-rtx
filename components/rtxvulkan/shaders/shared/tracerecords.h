#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TRACERECORDS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TRACERECORDS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/scene.h>

// What the trace's pipeline is built from beside its shaders: the specialization constants, and the
// records of its shader binding table. Included verbatim by both sides, for the reason
// `visibility.h` is.

#ifdef RTX_HOST

#include <array>

namespace Rtx::Shaders
{
#endif

    /// The trace's specialization constants, by `constant_id`: the frame's tuple, which every stage
    /// of a trace pipeline is handed, and after it the hit module's own pair, which each hit stage
    /// sets for itself. `lib/variants.glsl` and `visibilityhit.rchit` declare them by these names,
    /// and the host fills one table indexed by them, so no table's order has to agree with a
    /// declaration's.
    const uint SPEC_COUNTING = 0u;
    const uint SPEC_HAS_SUN = 1u;
    const uint SPEC_HAS_MOONS = 2u;
    const uint SPEC_HAS_SEA = 3u;
    const uint SPEC_HAS_MAPS = 4u;
    const uint SPEC_LAYERED = 5u;
    const uint SPEC_WATER = 6u;
    const uint SPEC_COUNT = 7u;

    /// How many closest-hit shaders the trace has: one for each `Rtx::MaterialKind`, in the order
    /// that enum names them, each standing behind `HIT_RECORD_LAYERS` records of the shader binding
    /// table.
    ///
    /// **The record is the kind and the layer, and traversal follows it.** `SceneAcceleration::placeRow`
    /// writes each instance's shader-table offset from its material's kind, so the hardware follows
    /// an index to the shader rather than the shader reading a material row to find out what it is —
    /// and the launch adds the layer it is tracing for, so the shader reads that off its record
    /// rather than off the payload.
    const uint HIT_SHADER_COUNT = 3u;

    /// What a hit record carries after its handle, which is everything a hit's stages are told by
    /// the launch that invoked them.
    ///
    /// **Nothing crosses the payload inwards.** The shader reads its eye and its layer off the record
    /// traversal chose for it, so the payload carries answers outwards and nothing else.
    ///
    /// **Each closest-hit shader stands behind a block of `HIT_RECORDS_PER_SHADER` of these**: one
    /// per eye the launch casts through, and within an eye's run one per layer of the peel. The
    /// instance's offset names the block, `hitRecordOffset` the record in it, and `hitRecordTable`
    /// is the one statement of what each holds.
    struct HitRecord
    {
        /// Which layer of the peel the shader is standing at, counting the pixel's first surface as
        /// nought. `PEEL_LAYERS` is the one past the last pane the launch peels, and a surface found
        /// there is drawn as the solid it stands in for whatever its own opacity says.
        ///
        /// **One budget a pixel, whichever eye spends it.** The world's ray behind a see-through arm
        /// starts at the layer the arms' stack ended on, so a pixel peels `PEEL_LAYERS` panes in all
        /// and traces no more than a pixel with no arms in front of it.
        uint mLayer;

        /// One where the ray was cast through `Eyes::mArms` and nought through
        /// `Eyes::mWorld`, which is what a stage reads its cone off: the arms' eye is wider than the
        /// world's, and a hit on the arms resolved at the world eye's pixel read a level too fine.
        uint mArms;
    };

    /// How many records an eye's run holds: one for the eye's own hit and one for each layer of the
    /// peel, which a trace adds to the run it is tracing through.
    const uint HIT_RECORD_LAYERS = PEEL_LAYERS + 1u;

    /// How many eyes the launch casts through: the world's and the arms'.
    const uint HIT_RECORD_EYES = 2u;

    /// How many records each closest-hit shader stands behind, and so what an instance's offset
    /// is its kind times: an eye's run of layers, per eye.
    const uint HIT_RECORDS_PER_SHADER = HIT_RECORD_EYES * HIT_RECORD_LAYERS;

    /// Which record of its kind's block a hit lands on, for a ray cast through the eye `arms` says
    /// at `layer` of the peel: what the launch adds to the instance's own offset.
    RTX_SHADER uint hitRecordOffset(uint arms, uint layer)
    {
        return arms * HIT_RECORD_LAYERS + layer;
    }

#ifdef RTX_HOST
    /// The whole hit table, kind by kind: what `VisibilityPass` hands the pipeline and what a test
    /// holds `hitRecordOffset` against.
    inline std::array<HitRecord, HIT_SHADER_COUNT * HIT_RECORDS_PER_SHADER> hitRecordTable()
    {
        std::array<HitRecord, HIT_SHADER_COUNT * HIT_RECORDS_PER_SHADER> records{};
        for (uint kind = 0; kind < HIT_SHADER_COUNT; ++kind)
            for (uint arms = 0; arms < HIT_RECORD_EYES; ++arms)
                for (uint layer = 0; layer < HIT_RECORD_LAYERS; ++layer)
                    records[kind * HIT_RECORDS_PER_SHADER + hitRecordOffset(arms, layer)]
                        = HitRecord{ .mLayer = layer, .mArms = arms };

        return records;
    }
#endif

    /// The sky, for the world's eye.
    const uint MISS_RECORD_SKY = 0u;

    /// Nothing, for the arms' eye. **The sky is seen through the world's eye alone**: the arms' eye
    /// traces the arms and nothing else, and a ray of it that finds no arm is not shaded — the
    /// world's own ray is traced there instead, so whatever reaches the sky reached it through
    /// `Eyes::mWorld`, and a pixel beside the arms runs the sky's shader once and not twice.
    const uint MISS_RECORD_UNSHADED = 1u;

    const uint MISS_RECORD_COUNT = 2u;

#ifdef RTX_HOST
    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
    static_assert(sizeof(HitRecord) == 8, "HitRecord must be scalar-packed on every side");
}
#endif

#endif
