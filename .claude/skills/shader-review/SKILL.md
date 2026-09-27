---
name: shader-review
description: Review the ray tracer's shaders (components/rtxvulkan/shaders, components/rtx/shaders) for correctness, physically based shading, ray tracing and Vulkan performance, and simplification, then apply every surviving finding and verify it. Use when asked to review, audit, optimise or simplify a shader, a shader library, a shared shader header, or a shader diff.
---

# Shader review

`AGENTS.md` governs: its rules, its verification and its measuring discipline apply
unchanged. This skill adds the checklists and the tree's shader facts that
`AGENTS.md` does not state.

## Scope

The paths given, or else the uncommitted diff under the two shader directories. If
both are empty, ask. Also read, not review: every included definition the scope calls,
and the host side of what it reads (the shared struct and its writer, the pipeline's
specialization constants, the host tests of shared functions).

## Procedure

1. Read the scope whole, with `docs/rtx/architecture.md` sections 8 and 10. The comments
   carry measured reasons. A finding against a measured reason needs new evidence.
2. Baseline before the first edit: `./omw kernels > <scratch>/before.txt` and
   `./omw shot --views=all --map --out=<scratch>/before`. For a light-transport
   finding, add a reference: `shot --accumulate=1000 --upscale=off --filter=false`.
3. Walk every function through `checklists/{correctness,pbr,performance,simplification}.md`.
   Record each candidate in `<scratch>/findings.md`: `file:line`, rule, fix, evidence.
4. Argue each candidate, then keep or drop it silently:
   - correctness: keep only with an input the tree can produce that gives a wrong value;
   - performance: *structural* (removes work) stays on `kernels` plus an unmoved `shot`;
     a *trade* (branch against select, 16-bit, unroll, group size, reorder hint) stays
     only after a bench that wins.
5. Apply every survivor. Change a shared function's host test with it. A redesign
   beyond the scope is reported, not built. Bugs outside the scope go to
   `.notes/ISSUES.md`.
6. Verify as `AGENTS.md` says, and read `kernels` and `shot` against the findings:
   every moved tuple belongs to a finding; a simplification or speed-up moves no
   picture unless a named rounding explains it; a correctness fix moves only its
   target pictures, toward the reference.
7. Report: the result first, then one line per finding (checklist, `file:line`,
   change, evidence), then what was reported but not built, and trades that lost.

Never commit. A clean scope is a normal result.

## Facts about this tree

- **Pinning** (`spirvpin.hpp`) refuses derivatives, `mediump`/`lowp`, a module's own
  float controls and unknown extended instructions. `precise` (`RTX_PRECISE`) is only
  for a value two shaders must compute to the bit (`rayAt`).
- **Shared headers** (`components/rtx/shaders/*.h`) compile as C++ and GLSL through
  `portable.h`. Host and device evaluate the same functions: `SpecularAlbedo`
  integrates the lobe `brdf.h` defines. A change on one side only is a bug. The buffer
  layout is scalar.
- **Variants** (`lib/variants.glsl`, and constants 6–7 in the hit module): a constant
  stands in front of its runtime test, never in its place. `HAS_MAPS` must leave
  every vanilla view unchanged. Each new tuple is one more pipeline to compile cold.
- **`visibility.rgen` holds hit objects: a subgroup operation there loses the device.**
  `reorderThreadEXT` is only in `RTX_SHADE`, under `REORDER`.
- The payload is fifteen packed words with one boundary (`packAnswer`,
  `unpackAnswer`); traversal has its own small payload. Hit shaders trace with ray
  queries, so `maxPipelineRayRecursionDepth` is 1; a `traceRayEXT` there needs it raised.
- Harness-only writes sit behind `COUNTING`. Every loop's bound is a shader constant
  (`LAMPS_AT_A_POINT`). Each random decision takes the next `SEED_*`, never a reused draw.
  Under DLSS the noise is a white hash, otherwise the blue-noise tile.

## Instruments `AGENTS.md` does not list

- Registers and spills: the verbose log's `pipeline <name>:` line for compute
  pipelines. NVIDIA reports nothing for ray tracing pipelines: use Nsight Graphics
  (`ngfx`), including its live-state view.
- `--validation=gpu` catches out-of-range descriptor indices.
- `--show=albedo|normal|roughness|specular` writes one surface input.
- `bench --reorder=…` and `bench --variants=false` measure the sort and the tuples.
- DLSS-RR's rules: `$NGX_ROOT/doc/DLSS-RR Integration Guide.pdf` (`NGX_ROOT` is in
  `build-debug/CMakeCache.txt`).

## Sources

NVIDIA [RT best practices](https://developer.nvidia.com/blog/rtx-best-practices/),
[RT best practices, updated](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/),
[shaders](https://developer.nvidia.com/blog/advanced-api-performance-shaders),
[intrinsics](https://developer.nvidia.com/blog/advanced-api-performance-intrinsics/);
Khronos [SER](https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder),
[descriptor indexing](https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html);
[GLSL 4.60](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html);
[Wächter–Binder offset](https://github.com/Apress/ray-tracing-gems/blob/master/Ch_06_A_Fast_and_Robust_Method_for_Avoiding_Self-Intersection/offset_ray.cu);
[ray cones 2021](https://www.jcgt.org/published/0010/01/01/);
[Filament](https://google.github.io/filament/main/filament.html);
[glTF BRDF](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation);
[VNDF caps](https://arxiv.org/abs/2306.05044);
[PBRT 4](https://pbr-book.org/4ed/contents);
[GPU hashes](https://www.jcgt.org/published/0009/03/02/);
[ReSTIR course](https://intro-to-restir.cwyman.org/).
