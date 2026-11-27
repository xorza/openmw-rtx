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
// thousand-and-twenty-four-texel field at two and a half units a texel, centred on the player and stepped
// sixty times a second, with `lib/water/ripples.glsl`'s springs, and so does this field: a step of a whole
// sixtieth whatever the frame rate (`RIPPLE_STEP_RATE` says why). The trace takes the same field and reads it
// as a wave tile (a slope and a curvature with a chain each), so a wake is lit, reflected and refracted the
// way a swell is, and focuses light on the bed under it as a swell does.

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
    const uint RIPPLE_STEP_BIND_MOVING = 3;
    const uint RIPPLE_STEP_BINDINGS = 4;

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

    /// The steps a second, which the springs below are stated against: upstream steps its field at
    /// it, and so does `RipplePass`, a whole step each, carrying a frame's part of one to the next.
    ///
    /// **One fixed step and not the frame's time**: a step of each frame's own length, Verlet
    /// corrected for the ratio of two steps, ran the checkerboard up by the ratio's swings, each
    /// step within the length a constant step is stable at. Frames alternating 12.5 and 20.8 ms
    /// took the field to a half's largest within a second and to NaNs across the window in twenty.
    const float RIPPLE_STEP_RATE = 60.0f;

    /// How many steps one frame may take of the field: a frame slower than that many sixtieths
    /// drops the rest of its time, as upstream drops whatever a frame takes past a sixtieth.
    const uint RIPPLE_FRAME_STEPS_MOST = 4u;

    /// How many impulses one step may stamp, from every frame since the last. Upstream keeps a
    /// hundred positions a step.
    const uint RIPPLE_IMPULSES_MOST = 128u;

    /// The height an impulse presses both heights toward, upstream's blobber's.
    const float RIPPLE_PRESS_DEPTH = -1.0f;

    /// The height under which the step writes nought: 2^-26.
    ///
    /// **Nothing the tiles can show**: the compose stores a texel's slopes, curvatures and squares,
    /// each at most 0.64 of the largest height about it in magnitude, the curvature `4 h / 2.5²`
    /// the largest — so under this every stored value is under 2^-25, which a half holds as nought
    /// whether its store rounds to the nearest or toward nought. **And what lets a still field be
    /// still**: without it a decaying field ends in subnormals that rounding holds up for ever, and
    /// a field that is exactly nought is what `RipplePass` stops stepping.
    const float RIPPLE_STILL = 1.0f / 67108864.0f;

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

        /// How wide the stamp is, in texels.
        float mRadius;

        /// How long it presses, in sixtieths: the time of the frame that gave it, which a step
        /// presses for (`ripplestep.comp`'s `pressed`).
        float mPress;
    };

    /// What the step is told: how far the field's window moved since the last step, in texels,
    /// so the old field is read where it now stands, how many impulses to press on the way out,
    /// and `mReport` on the frame's last step, which says whether any of the field still moves.
    /// The springs, the step and the texel are the constants above, which both kernels read for
    /// themselves.
    struct RippleStepConstants
    {
        ivec2 mShift;
        uint mCount;
        uint mReport;
    };

#ifdef RTX_HOST
    static_assert(sizeof(GpuRippleImpulse) == 16, "GpuRippleImpulse must be scalar-packed on every side");
    static_assert(sizeof(RippleStepConstants) == 16, "RippleStepConstants must be scalar-packed on every side");
}
#endif

#endif
