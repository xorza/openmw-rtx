# To do

## Code

- Remove the visibility limit.
- Custom map notes plugin.
- Audit macros.
- `#if OPENMW_FFMPEG_5_OR_GREATER`.
- Audit `#if`s, remove legacy.
- Order includes canonically.
- No upscaler.
- unsigned int to uint32, etc
- 64 bit build?

## Open questions

- **Whether a tour runs unsettled, as the game walks** (moved out of the tree-and-content-memory plan, 2026-11-18): a
  settled walk waits on the frame for its nearest cell and its ground's bake (`CellSupply::waitForOne`, 111 ms of a
  135 ms frame on the desk's Repack tour), which no player waits for. Decide by an A/B of worst frames and outruns on
  the handheld, then fly the baseline again (`docs/research/2026-11-17-longest-frames.md`).

- automate memleak detection
- Upstream merge's app token: the exchange is copied from claude-code-action and undocumented (`.github/actions/app-token`). An app of our own (`actions/create-github-app-token`), or CI dispatched with the workflow's token?

## Performance

- **The ring reads what can stand** (moved out of the tree-and-content-memory plan, 2026-11-18): the handheld's Repack
  tour at `b461faf024` loaded 27,004 `Nif`s, 8,828 of them again (33%), and 25,661 `Node`s, 7,494 again (29%). A
  cell's references read as a prefix by radius, one band ahead of the size rule, with `CellRing` asking the reader to
  go further as the cell nears. It needs each model's radius in the store's manifest before the model is read, and
  `CellPlacer` adopting a cell in parts. Held by the count again, `repeat` and `shot`, and no outrun.

- `x86_64-v3`.
- Publish rgbcx.
- AVX encoders.
- Bake distant land into one mesh.

## Prompts

```text
/optimize-perf rtx renderer, make sure implementation is canonical and idiomatic, see if you find inconsistencies or issues - fix them,
try and reduce register and spill memory usage, analyze rtx shaders, denoiser, fsr. etc.  bench and profile vanilla, mfr and pbr,
make sure to test and bench on  steamdeck deck@192.168.0.31 and win 192.168.0.8, optimize kernels, registers and reduce spills there as well,
test. investigate any issues incountered for canonical and idiomatic fixes and apply them. at the end run tour for all the mw flavors on all 3 devices and analyze logs and performance
```
