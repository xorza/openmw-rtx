#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_VARIANTS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_VARIANTS_GLSL

#include "visibility.h"

// What kind of frame this is, told to the compiler rather than to the branch predictor.
//
// **The trace is occupancy-bound, so a path nothing takes still costs the pixels that take
// another.** One kernel serving an interior with no sun, no moons and no sea spends the registers
// the moons need on every pixel of that room, and taking the moons out alone is worth a real share
// of the trace in a room no moon ray is ever traced in.
//
// **What the tuples are worth, measured** (`bench --views=all --variants=false` against the
// default, five legs each way interleaved on a hot card, 2026-09-20, the trace zone's median):
// three per cent in a daytime exterior — `one-cell-walk` 1.58 against 1.63 ms, `seyda-neen-shore`
// 2.58 against 2.66 — where the full tuple carries the moons' code the day never runs; nothing
// measurable in a room, where the legs spread by a quarter either way and the guild's medians
// came out 3.74 against 3.66. What they cost is launches for the driver to compile on a cold
// start: fourteen more than the full tuple alone was eight seconds of creation and thirteen of its
// second compile, against 2.3 s and 1.1 s. `HAS_MAPS` doubles the trace's half of the table. Kept
// for the exteriors' three per cent and for a vanilla frame's own kernel; the switch is what
// measures it again.
//
// **Each of these stands in front of the runtime test it replaces and never in place of it.** True
// leaves the shader exactly as it was. False is set only where the test behind it already answers
// no, so what the compiler removes is dead code rather than an answer: a specialized frame computes
// the arithmetic the one kernel does. **Not always to the bit**, pinned modules and all
// (`spirvpin.hpp`): the driver compiles what is left differently around what went, and
// `--variants=false` against the tuples moved the trace of 22 views of 23 by a rounding — a level
// of a byte at 37 pixels of the picture at most (2026-09-28).
//
// `Rtx::VisibilityVariant` is the other half. It reads each of these off the frame's own constants,
// and `VisibilityPass` keeps one pipeline per tuple.

/// Whether the frame counts for the host — `counts.h`: the primary rays that hit something, and
/// the values that were not finite at each boundary they crossed.
///
/// **A harness facility, so the game's module does not carry the atomics at all.** `shot` prints the
/// hits, `bench` reports them, `check` asserts the finiteness and a test asserts on both, and
/// nothing in the game ever reads either — so an unconditional `atomicAdd` was a debug write
/// compiled into the shipping kernel. Specialized rather than branched on a uniform because the
/// branch is what has to go, not just the write: with this false the constant folds away and the
/// buffer is never touched.
///
/// **Counted as misses, in the miss shader, and turned into hits on the host.** Every lane adding
/// to one word serialises at that word: a million hits took four tenths of a millisecond of the
/// trace in a room where every ray hits, which the shading of a street hid and a room's did not — a
/// harness figure that read the room's frame nine percent slow. The sky's shader runs exactly once
/// for every primary ray that ends in nothing, so the misses are the same count from the other side,
/// and a room adds nought. A ballot would add one word a subgroup instead, but the launch calls
/// `traceRayEXT` between its reads, and what a subgroup holds on either side of a trace is the
/// driver's to regroup: the miss shader counts the ray where it ends.
layout(constant_id = SPEC_COUNTING) const bool COUNTING = false;

/// Whether the sun is over the horizon: the constant half of `sunUp`, which says the rest.
layout(constant_id = SPEC_HAS_SUN) const bool HAS_SUN = true;

/// Whether either moon is drawn or lights anything. Both a disc with an alpha and a light with an
/// irradiance, because the sky draws one where the surfaces are lit by neither.
layout(constant_id = SPEC_HAS_MOONS) const bool HAS_MOONS = true;

/// Whether this frame holds any water: a surface the eye can meet, or a level the eye can stand
/// under. False takes the waves, the caustics and the whole underwater column out of a room.
layout(constant_id = SPEC_HAS_SEA) const bool HAS_SEA = true;

/// Whether this frame's scene places a material with a normal map or a specular map. False takes
/// the tangent fetch, the maps' reads and the whole specular half out of a scene that has none —
/// every vanilla scene.
///
/// **What keeps a vanilla frame the frame it was, and not only what keeps it fast.** The maps' code
/// sits beside the Lambert surface's in the same functions, and compiled in, it moved how this
/// driver compiled the Lambert arithmetic around it even where not one hit ran it: every vanilla view
/// traced a different frame by a rounding. Specialized out, the kernel a vanilla scene runs is the
/// one it ran before the maps existed.
layout(constant_id = SPEC_HAS_MAPS) const bool HAS_MAPS = true;

// `SPEC_LAYERED` and `SPEC_WATER` are the hit module's own — `visibilityhit.rchit` — and stand after
// these in the one table every stage of a pipeline is handed.

#endif
