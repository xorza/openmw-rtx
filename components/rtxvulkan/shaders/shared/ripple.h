#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_RIPPLE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_RIPPLE_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What walked through the water: a height field around the eye, stepped by the wave equation and
// stamped where an actor wades, a projectile lands or a spell strikes, and read by the sea as one
// more tile beside its cascades.
//
// **The rasterizer's own simulation, in its own numbers.** `MWRender::RipplesSurface` runs a
// thousand-and-twenty-four-texel field at two and a half units a texel, centred on the player and
// stepped sixty times a second, with `lib/water/ripples.glsl`'s springs. The trace takes the same
// field and reads it as a wave tile: a slope and a curvature with a chain each, so a wake is lit,
// reflected and refracted the way a swell is, and focuses light on the bed under it the way a
// swell does.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `ripplestep.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint RIPPLE_STEP_BIND_BEFORE = 0;
    const uint RIPPLE_STEP_BIND_AFTER = 1;
    const uint RIPPLE_STEP_BIND_IMPULSES = 2;
    const uint RIPPLE_STEP_BINDINGS = 3;

    /// Where `ripplecompose.comp` binds what it reads and writes in set 0, and how many there are.
    /// The shader's layout and the pass's own layout and writes are numbered by these and by
    /// nothing else, so the two cannot drift apart.
    const uint RIPPLE_COMPOSE_BIND_FIELD = 0;
    const uint RIPPLE_COMPOSE_BIND_SURFACE = 1;
    const uint RIPPLE_COMPOSE_BIND_CURVATURE = 2;
    const uint RIPPLE_COMPOSE_BINDINGS = 3;

/// What the field the step writes and the compose reads is stored as: a texel is the height and the
/// height a step before.
#define RIPPLE_FIELD_FORMAT STORAGE_RG32F

    /// Texels along each axis of the field.
    const uint RIPPLE_GRID = 1024u;

    /// World units one texel covers, which with the grid is how far the field reaches: 2560 units
    /// across, so 1280 from the eye in every direction.
    const float RIPPLE_TEXEL = 2.5f;

    /// The rate the springs below are stated against, a second: upstream steps its field at it,
    /// and a step of `dt` is `RIPPLE_STEP_RATE × dt` of these (`RippleStepConstants`).
    const float RIPPLE_STEP_RATE = 60.0f;

    /// How many steps one frame may take of the field, each no longer than the springs stay stable
    /// over (`RipplePass::getLongestStep`): a frame slower than that many of the longest steps drops
    /// the rest of its time, as upstream drops whatever a frame takes past a sixtieth.
    const uint RIPPLE_SUBSTEPS_MOST = 4u;

    /// How many impulses one frame may stamp. Upstream keeps a hundred positions a frame.
    const uint RIPPLE_IMPULSES_MOST = 128u;

    /// The side of the square workgroup every ripple dispatch runs on.
    const uint RIPPLE_WORKGROUP = 16u;

    /// The springs `lib/water/ripples.glsl` states, tuned there by eye to look like water: the
    /// neighbour coupling (`a`), and the two dampings on the height (`udamp`) and its velocity
    /// (`vdamp`). The grid, the texel and these are the rasterizer's, and a GLSL header can
    /// include none of them: `RtxRipplesTest` holds each equal to its source.
    const float RIPPLE_STIFFNESS = 0.28f;
    const float RIPPLE_HEIGHT_DAMPING = 0.04f;
    const float RIPPLE_VELOCITY_DAMPING = 0.04f;

    /// One impulse, in the field's own texels.
    struct GpuRippleImpulse
    {
        /// Where it lands, in texels from the field's origin.
        vec2 mAt;

        /// How wide the stamp is, in texels, and how deep it presses.
        float mRadius;
        float mStrength;
    };

    /// What the step is told: how far the field's window moved since the last step, in texels,
    /// so the old field is read where it now stands, how many impulses to press on the way out,
    /// and how long a step it is. The springs and the texel are the constants above, which both
    /// kernels read for themselves.
    ///
    /// **A step of `s` sixtieths after one of `p`, as time-corrected Verlet takes it**: the height
    /// moves on by what it moved over the last step, `s / p` of it, under the velocity's damping
    /// over `s` sixtieths, `(1 − vdamp)^s`, which is `mCarry`; and by the springs' pull and the
    /// height's damping, stated per sixtieth squared, over `s (s + p) / 2` of them, `mScale`. At
    /// `s = p = 1` that is `applySprings` to the term.
    struct RippleStepConstants
    {
        ivec2 mShift;
        uint mCount;
        float mCarry;
        float mScale;
    };

#ifdef RTX_HOST
    static_assert(sizeof(GpuRippleImpulse) == 16, "GpuRippleImpulse must be scalar-packed on every side");
    static_assert(sizeof(RippleStepConstants) == 20, "RippleStepConstants must be scalar-packed on every side");
}
#endif

#endif
