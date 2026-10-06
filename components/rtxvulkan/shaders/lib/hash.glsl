#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HASH_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HASH_GLSL

// The hashed counters every draw that is not the blue tile's comes from, and the seeds that keep
// their sequences apart: what a pass that binds none of the trace's tables draws with too, the
// wavelet's tap offsets among them. `random.glsl` reads them with the frame the trace holds.

#include "scene.h"

/// Which sequence a lamp reservoir draws on. **One per depth, because a path shades twice** — the
/// hit the eye found and the hit its bounce found — and two reservoirs stepping the same sequence
/// would choose correlated lamps at both ends of it. These are seeds for `randomSeed` rather than
/// channels of the tile, which has only `RANDOM_STREAMS` of them and answers a different question.
///
/// **Each one is the one before it and one more, and none of them is a number written out.** What a
/// seed owes the others is only that it differ from them, and a list of literals states that in a
/// way nothing checks — two of them spelled alike compile, and the two reservoirs then draw one
/// sequence and keep the same lamps.
const uint SEED_LAMPS_EYE = 0x51u;
const uint SEED_LAMPS_BOUNCE = SEED_LAMPS_EYE + 1u;

/// And a third for the pane the eye is looking through, which shades beside the surface behind it
/// and would choose the same lamp at both if it stepped the same sequence. `SEED_LAMPS_PANE_DEEPER`
/// is where the layers under it draw from.
const uint SEED_LAMPS_PANE = SEED_LAMPS_BOUNCE + 1u;

/// Water shades two surfaces from one hit — what it reflects and what is seen through it — and each
/// of them opens a reservoir of its own. **Two constants and not one**, because two reservoirs
/// seeded alike step the same sequence and keep the same lamps.
const uint SEED_LAMPS_MIRROR = SEED_LAMPS_PANE + 1u;
const uint SEED_LAMPS_THROUGH = SEED_LAMPS_MIRROR + 1u;

/// And one more for the direction a path's end asks the sky about, which is not a lamp at all.
///
/// **A sequence of its own, because it is drawn beside a reservoir and not out of one.** Stepping
/// the lamps' would move which lamp a hit chose every time the hemisphere was asked a question, and
/// the two have nothing to do with each other.
const uint SEED_AMBIENT_REACHING = SEED_LAMPS_THROUGH + 1u;

/// And one for the lamp a froxel of the air holds out of every lamp reaching it.
const uint SEED_LAMPS_FOG = SEED_AMBIENT_REACHING + 1u;

/// And one for which way a froxel's ambient ray goes. Apart from the lamp's above, which is drawn
/// in the same breath: seeded alike, where the lamp is and where the sky is would move together.
const uint SEED_AMBIENT_FOG = SEED_LAMPS_FOG + 1u;

/// And one for which face of a sheet the eye's bounce leaves by.
///
/// **Beside the bounce's own pair and not out of it.** The direction is drawn from the tile's
/// `STREAM_BOUNCE`, which has two channels and no third; a side taken from one of them would tie
/// which face is asked to where in the hemisphere it is asked, and a sheet's two faces would be
/// sampled as two halves of one hemisphere rather than as two hemispheres.
const uint SEED_SHEET_SIDE = SEED_AMBIENT_FOG + 1u;

/// And one for whether an indirect hit is lit at all this frame.
///
/// **An entry of the pixel's own chain rather than a step of the reservoir's sequence.** The draw is
/// made before the reservoir opens, and taken as a step of it, it would move every lamp the bounce
/// hit weighed — which is the ordering `gather` states at the top of itself.
const uint SEED_INDIRECT_LIGHT = SEED_SHEET_SIDE + 1u;

/// And one for each layer of the peel under the first: a cuirass over a skirt over a leg shade
/// beside each other as well as beside the body under them, and a stack stepping one sequence would
/// light every layer of a person from the same lamp.
///
/// **At the end of the chain rather than beside `SEED_LAMPS_PANE`**, because this block is
/// `PEEL_LAYERS - 1` wide: written where the pane's own seed is, every seed after it would move each
/// time the peel's budget did, and a seed that moves is every sequence in the frame redrawn.
const uint SEED_LAMPS_PANE_DEEPER = SEED_INDIRECT_LIGHT + 1u;

/// Which of them the `layer`th peeled surface draws its lamps from, counting the nearest as nought.
uint paneSeed(uint layer)
{
    return layer == 0u ? SEED_LAMPS_PANE : SEED_LAMPS_PANE_DEEPER + layer - 1u;
}

/// And one for what each layer of the peel sees of the ambient, a block `PEEL_LAYERS` wide after
/// the lamps' own, for the reason that block is where it is.
const uint SEED_AMBIENT_PANE = SEED_LAMPS_PANE_DEEPER + (PEEL_LAYERS - 1u);

uint paneAmbientSeed(uint layer)
{
    return SEED_AMBIENT_PANE + layer;
}

/// And one for whether the eye's bounce is traced at all this frame.
///
/// **After the two blocks, so that adding it moved no sequence already drawn.** Offset from the
/// pixel's key rather than drawn out of the bounce's own pair or the sheet's side, for the reason
/// `SEED_INDIRECT_LIGHT` is: a draw taken as a step of another sequence moves everything that
/// sequence decides.
const uint SEED_BOUNCE_TRACED = SEED_AMBIENT_PANE + PEEL_LAYERS;

/// And one for which half of a glossy surface the eye's bounce samples, its lobe or its diffuse.
///
/// **Beside the bounce's pair and not out of it**, for the reason `SEED_SHEET_SIDE` gives: the
/// pair draws the direction within whichever half is chosen, and a choice made from one of its
/// channels would tie which half to where in it. Drawn only by a surface with a specular half, so
/// a Lambert surface draws what it drew before there was one.
const uint SEED_BOUNCE_LOBE = SEED_BOUNCE_TRACED + 1u;

/// And one for where in the sun's disc a water shaft's shadow rays aim.
///
/// **Beside the march's own offset and not out of it.** The offset is one number, and a pair carried
/// from it lies on one line of the square: every step's ray would aim along one spiral of the disc,
/// and a rock's edge across the disc would shadow the shaft by the share of that spiral it covers and
/// not by the share of the disc.
const uint SEED_WATER_SHAFT = SEED_BOUNCE_LOBE + 1u;

/// And the occlusion rays at the far end of the water's two rays, the reflection's and the
/// refraction's, for the reason `SEED_AMBIENT_REACHING` gives. Entries of the chain and not the
/// lamp seed plus `SEED_AMBIENT_REACHING`, which is a sum of two entries and so nothing the chain
/// says is apart from the rest.
const uint SEED_AMBIENT_MIRROR = SEED_WATER_SHAFT + 1u;
const uint SEED_AMBIENT_THROUGH = SEED_AMBIENT_MIRROR + 1u;

/// And which ray's shadowed sources a pixel of water keeps the bit of, `mixSplit`'s draw: once
/// between the water's two rays, and once between the water and the bed under a waterline.
/// **Two**, since a waterline pixel draws both, and one number deciding both would tie which ray
/// the water keeps to whether the bed was kept.
const uint SEED_SHADOWED_LEGS = SEED_AMBIENT_THROUGH + 1u;
const uint SEED_SHADOWED_SHORE = SEED_SHADOWED_LEGS + 1u;

/// Which texels under the cut of a soft edge the eye meets (`cutAt`): one draw a candidate, keyed on
/// its triangle as well, so two soft layers on one pixel are met apart.
const uint SEED_SOFT_EDGE = SEED_SHADOWED_SHORE + 1u;

/// And one for whether a ray that commits meets a see-through surface (`MEET_BY_CHANCE`), drawn off
/// the ray's own key and not a pixel's.
const uint SEED_SEE_THROUGH = SEED_SOFT_EDGE + 1u;

/// And one for where the wavelet's taps stand at its widest step (`ATROUS_JITTER_STEP`), drawn off the
/// pixel and the frame the wavelet is handed: its own, so where a level's taps stand says nothing
/// about the draws that lit the pixel.
const uint SEED_WAVELET_TAPS = SEED_SEE_THROUGH + 1u;

/// A key for one pixel, which a caller offsets by a `SEED_` constant to say which sequence it wants.
///
/// Two odd multipliers rather than two shifts: a shift leaves the low bits of one axis where the
/// other's are, and two pixels a power of two apart then share a prefix.
uint pixelKey(uvec2 pixel)
{
    return pixel.x * 73856093u ^ pixel.y * 19349663u;
}

/// A key for one froxel of the fog volume, which a caller offsets the same way.
///
/// **A third multiplier and not a shift of the pair above**, for the reason that one gives: a shift
/// would leave the depth's low bits where a column's are, and two froxels a power of two apart down
/// one ray would then draw what two columns a power of two apart across the screen draw.
uint froxelKey(uvec2 column, uint slice)
{
    return pixelKey(column) ^ slice * 83492791u;
}

float randomNext(inout uint state)
{
    state = state * 747796405u + 2891336453u;

    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word ^= word >> 22u;

    // Twenty-four bits, which is every one a float can hold without rounding two of them together.
    return float(word >> 8u) * (1.0 / 16777216.0);
}

/// A key stepped by the multiplier every sequence here starts from.
///
/// **The whole of what a pattern that must not move needs, and the first half of `randomSeed`.**
/// Rain rings are keyed on the cell they fell in and have to stay there from one frame to the next,
/// so the frame is exactly what they may not mix in — and a second spelling of this step is a
/// second idea of what a key is worth.
uint steppedKey(uint key)
{
    return key * 0x9E3779B9u;
}

/// The state a stream of draws starts from, for `key` on frame `frameNumber`: `randomSeed`'s, for a
/// pass that is handed its frame rather than reading the trace's.
uint seededKey(uint key, uint frameNumber)
{
    uint state = steppedKey(key) + frameNumber * 0xC2B2AE35u;
    state ^= state >> 16u;
    state *= 0x7FEB352Du;

    return state;
}

#endif
