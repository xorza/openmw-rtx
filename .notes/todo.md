# To do

## Code

- Remove the visibility limit.

- **The seam's structure, a proposal of its own** (the user's decision, 2026-11-20; found by the renderer's review):
  - `MWRender::Renderer` holds seven responsibilities and state of its own: 22 of 55 virtuals have work in one
    renderer, and the base reads settings and the wall clock and sleeps. The pacing (`awaitFrame`, `openFrame`,
    `openNestedFrame`, `FramesAside`, the limiter) as a concrete pacer the engine owns; the factory calls and world
    events stay. `RendererTest`'s wall clock and settings go with it.
  - A renderer is made by ten ordered calls (`Engine::prepareEngine`, `RenderingManager`'s constructor), each guarded
    by an assert, every state between two representable: what exists before the window in `RendererSpec`, resources
    with the one call that needs them.
  - A declined request has two sources, a virtual that does nothing and `RenderSupport`'s table keyed by strings: one
    call that answers its refusal, and the chain's callers taking the post-processor from one accessor that carries
    the reason beside the null.
  - `RtxRenderer`'s phase rule is spelled two ways, and `updateTraversal` and `renderGui` take `Between` alone: named
    expectations a caller kind.

## Performance

- **The sea per tile** (moved out of the one-kernel plan, 2026-11-19): the stages past the trace take the sea from the
  frame, which a room with none already escapes; classed per tile, a view by the sea would run its dry tiles without
  it, which needs the landing classed by whether a bounce lands in water. And whether the trace keeps a constant for
  its stop at the sea, which held it a tier up on AMD's Windows driver while it resolved its solid, to be measured
  again (`docs/research/2026-11-19-one-kernel-registers.md`).
  The sea's code outside its own stages costs 0.09 ms at the ship and 0.34 at the shore on the desk, 1.1 and 2.25 ms
  of 48 and 35 ms frames on the handheld, all tiles counted: the most a class by the sea saves, on the dry tiles alone
  (`docs/research/2026-11-20-tour-pacing-ring-reads-sea-tiles.md`).

## Prompts

```text
/optimize-perf rtx renderer, make sure implementation is canonical and idiomatic, see if you find inconsistencies or issues - fix them,
try and reduce register and spill memory usage, analyze rtx shaders, denoiser, fsr. etc.  bench and profile vanilla, mfr and pbr,
make sure to test and bench on  steamdeck deck@192.168.0.31 and win 192.168.0.8, optimize kernels, registers and reduce spills there as well,
test. investigate any issues incountered for canonical and idiomatic fixes and apply them. at the end run tour for all the mw flavors on all 3 devices and analyze logs and performance
```
