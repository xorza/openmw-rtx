# Denoiser nondeterminism: root cause and proposed fix

Machine: RTX 4090 Laptop (AD103, SM 8.9, 76 SMs), driver 615.71.09, Arch Linux, KDE Wayland.

## Result

**The fault is not in the renderer.** The GPU runs the same machine code on bit-identical inputs
and push constants, and sometimes computes different low-order bits. This occurs only in the first
dispatch after a pipeline drain, and only while the GPU is in a state where every drain takes about
1 µs longer than usual. The GPU enters that state in stretches of 20 to 50 frames, and only under
sustained load: the queue is never empty and the GPU never idles between frames.

The part that the application can see is proven. The mechanism inside the GPU is not visible from
the application. The data fits one explanation: an SM idle power state (clock or power gating)
that the SMs enter at a drain, with a wake-up that the first heavy dispatch does not survive
without error. The SM frequency, the power level and the power-cap state do not change in that
state, so a clock or voltage step (DVFS) does not explain it.

This is silent data corruption: wrong floating-point results with no error reported. It is a
matter for NVIDIA (driver, firmware or silicon), and possibly for the laptop's power tuning.

## The symptom

- `./omw shot --views=all --upscale=off` is not repeatable with the denoiser on. On the same
  build, about half of the runs give a different `g-direct` hash in 2 to 7 views. The set of
  views changes from run to run. The trace channels and the scene digest are the same in every
  run.
- In the picture, the difference is 1 of 255 at a few pixels. In the image data, it is exactly
  1 half-float step in 13 to 30 texels of 2 million. There is no NaN and no infinity.
- The old code (before the redesign commit `720f016a5c`) has the same fault at the same rate.
- `./omw repeat` does not see it, because `repeat` runs with `--filter=false`.

## The chain of evidence

### 1. Where the difference enters

- With `--filter=false`, 7 full runs (1624 frames) are identical. The fault is on the denoised
  path.
- The first image to differ is always the output of the wavelet's first level (`atrous.comp`,
  level 0, which is also the colour history). The accumulator's moments and the pane mean never
  differ. The colour history then carries an event forward through later frames.

### 2. One dispatch, run three times on the same inputs

A "twin" test inside the renderer records the same wavelet dispatch three times in one frame, with
the same inputs and constants, into separate images. A check pass compares them on the GPU.

- Only the **first dispatch after a full barrier (a drain)** differs. The second and the third,
  which follow with no barrier, never differ from each other (0 texels in about 45 000 frames).
- At full float precision, an event changes 2% to 10% of the texels by 1 to 2 ulps in the median.
- The differing workgroups cover one part of the dispatch in launch order: in some events the
  early part, in others the late part. So a time window overlaps the dispatch.

### 3. Same bits in, same code, different arithmetic

- **Inputs.** A probe copy of the kernel hashes every value that it fetches from its three input
  images. In every sampled differing texel, the input hash is bit-identical between the first and
  the second dispatch, and the hash of the per-tap weights differs. So the fetched data is the
  same and the arithmetic result is not.
- **Constants.** A hash of every push-constant word never differs. Every probe dispatch also
  carries a push constant unique to its frame and position and logs its warps under it: every slot
  holds exactly its own warp count (64 800 or 128) in 11 000 frames, with events among them.
- **Writes.** A sentinel in the target is never left. Every texel is written.
- **Machine code.** The register count and binary size never change. A dataflow check over the
  disassembled SASS (driver cache, `nvdisasm -b SM89`) finds no scoreboard, register or predicate
  hazard.
- **The code matters.** A few extra instructions in the probe (more hash code) make it stop
  failing for the whole build, while the unchanged `atrous.comp` in the same frame still fails.
  Only kernels with enough arithmetic per tap fail.

### 4. The trigger is the queue state, not the load level

Measured on one binary, runs interleaved:

| Queue mode | Failing runs | Power (median) | Power-cap share |
|---|---|---|---|
| Pipelined (normal) | 10 of 12 | 143.7 W | 0.85 |
| `vkDeviceWaitIdle` after each frame's submit | 0 of 12 | 141.8 W | 0.80 |
| `vkQueueWaitIdle` before each submit | 0 of 12 | 142.5 W | 0.83 |
| Wait after each frame, beside a second GPU-bound process | 3 of 3 | | |
| Wait before each submit, beside a second GPU-bound process | 3 of 3 | | |

The waits leave the power, the power-cap share and the host's CPU work almost the same (the
wait-before-submit mode still records the next frame while the GPU works). What they remove is
the GPU's busy-to-idle pattern: with a wait, the GPU idles between frames. A second GPU-bound
process removes those idle gaps again, and the fault returns.

### 5. The slow-drain state

A GPU timestamp before and after the drain in front of each twin dispatch measures the drain.

- The drain time is bimodal. State A: 1.4 to 1.9 µs. State B: 2.0 to 3.9 µs. In 22 278 trios,
  state A holds about 19 000 and has 0 events. State B holds about 3 200 and has every event
  (about 1.5% of its trios).
- State B is a GPU-wide state. It lasts for stretches of 20 to 47 frames (the "bursts"), and in
  those frames every drain is slower (at the frame's start: 3.5 µs against 1.6 µs).
- Pipelined runs spend 13% to 17% of their frames in state B. Runs with a wait per frame spend
  0.6% to 1.2%, in isolated frames, and have no events.
- More state B, more events: runs with 21, 43 and 248 slow drains at the frame's start had 0, 7
  and 28 events.
- The SM frequency (shader clocks: SM cycles against the device's real-time clock, summed over
  every warp) is the same in state A and state B, 2315 against 2316 MHz. It is also the same in the
  failing first dispatch and in the dispatches after it.
- No drain long enough to hold a context switch (30 µs or more) has an event. So the fault is not
  a context switch to another process at the drain.

### 6. What protects

- A dispatch of a pipeline with the same layout, recorded just before the wavelet with no barrier
  between them, prevents the event. That dispatch keeps the SMs busy across the point where the
  wavelet starts. A light kernel of another pipeline does not protect. A change of pipeline
  configuration can need the SMs to drain, which gives the same idle point again.

## What is ruled out

| Candidate | Evidence against it |
|---|---|
| The redesign (`720f016a5c`) | The old code fails at the same rate. |
| Missing barrier between passes | Sync validation reports nothing. A full memory barrier before every wavelet level does not help. |
| Discard with an empty first scope (`sUndefined` source stage `NONE`) | With `ALL_COMMANDS` as the source stage, it still fails. |
| Stale or incomplete inputs, any cache | The hash of every fetched input value is identical in the failing dispatch. |
| Lost or late writes | A sentinel in the target is never left. |
| Push constants: range, staleness, another frame's commands | The push-constant hash never differs. Each dispatch logs under its own unique constant, with exact warp counts. |
| Wrong eye (arms camera) selected per tap | With the arms camera equal to the world camera bit for bit, the rate is the same. |
| Warm-up, streaming, map tiles | A history reset on the first measured frame does not stop it. Runs without `--map` fail. |
| Present, window, interface | Runs without a window fail. Skipping the GUI draw changes nothing. |
| Deferred uploads, extra submits | Event frames are ordinary frames. One extra submit per frame (empty, a barrier, a fill, or both) still fails in 7 or 8 runs of 8. |
| A second compiled variant, a hazard in the machine code | Register count and binary size never change. The dataflow check finds no hazard. |
| Faulty SM | Differences spread evenly over all 76 SMs. |
| Core clock or voltage steps (DVFS), the power cap | Same SM frequency in state A and B and in every dispatch of a trio. Wait modes at the same power and power-cap share do not fail. |
| Core clock margin | Still fails with the core clock locked to 1800 MHz. |
| Memory clock switches | The memory clock stays at 9001 MHz through every burst. |
| Context switch to another GPU process at the drain | No long drain has an event. |
| The harness's NVML card watch | With the watch off (`RTX_NONVML=1`), 7 of 8 runs still fail. |

Two earlier results do not hold, and are corrected here:

- A batch with a barrier and a fill submitted after each frame had 0 of 8 failing runs. The same
  setting on a later build fails in 8 of 8. The first result came from a build that did not fail,
  like the probe builds in section 3.
- The standalone GPU test (`RtxAtrousReproTest`) never failed, so it rules nothing out. It was
  never shown to reach state B.

## Decision

The denoiser stays as it is. Its nondeterminism is documented in `docs/rtx/architecture.md` (the
denoiser), and the harness holds each part to what it promises:

- `FrameHashes` records per frame whether the denoiser composed it (`hashes 5`). Between two
  denoised runs, a difference in the composed frame or the picture where no other column moved is
  reported as the card's arithmetic under the wavelet, and is not a verdict. One run denoised and
  the other not is two configurations.
- `shot --against` holds a picture the wavelet put together (a denoised frame with no upscaler, a
  doll, a map tile) to within `sDenoiserNoiseLevels` (one level of 255), and every other picture
  exactly.
- `repeat` and the gate are unchanged: they run with the filter off, where every frame is exact.

What this costs: with the filter on, a real change of exactly one level in a denoised picture, or
a change `bench --against` sees only in the composed frame, is not caught. A trace change is still
checked exactly, because every channel the trace writes stays a verdict.

Not built: a guard dispatch before level 0, a serialized queue for the harness, and a report to
NVIDIA. The evidence above is what such a report would need, with a standalone reproduction that
first reaches state B.

All the investigation instrumentation is removed from the tree.
