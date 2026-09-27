# Performance

The trace is occupancy-bound: registers decide how many warps run. **S** = structural
(removes work), **T** = trade (bench it).

## Payload and live state

- **S** A payload field costs every ray: pack it (halves, octahedral, bits) or remove it.
- **S** A value computed before `traceRayEXT`, `hitObjectTraceRayEXT`, `reorderThreadEXT` or a `rayQueryProceedEXT` loop and read after it spills: move it after, or recompute it.
- **S** Keep work before `rayQueryInitializeEXT` and inside the candidate loop to what the loop needs.

## Traces

- **S** Several trace call sites become one call in a loop.
- **S** Divergent lanes trace unconditionally with `tMax = 0` or an empty mask, not behind a branch.
- **S** Visibility rays end at the first hit.
- **S** Geometry with no cutout is flagged opaque; split a mesh's opaque and cutout parts.
- **S** A shader test that an instance mask could answer moves to the mask.

## Any-hit

- **S** Only a texture read and a compare; minimal dependent loads before the alpha fetch.
- **T** The alpha read's level: coarser saves cache but changes the cutout.

## Reordering

- **T** Pays on divergent rays (bounces, rough reflections), costs on coherent ones.
- **T** Hints use few bits: material flag, texture slot, early exit. Never shader type or hit/miss.
- **S** Live state across `reorderThreadEXT` is saved and restored: trim it.

## Compute

- **S** Workgroup a multiple of 32, at least 64; 8×8 or 16×8 for images.
- **S** Tail lanes do no work.
- **T** Shared memory per group limits occupancy; pad strided arrays against bank conflicts.
- **T** Subgroup ops before shared memory, never in `visibility.rgen`.
- **T** Adjacent lanes read adjacent addresses; hot records in 8- or 16-byte loads (scalar-layout `vec3` arrays split).
- **T** Thread-group swizzle on large image passes.

## Arithmetic

- **S** Integer `pow` becomes multiplies; `exp`/`log` become `exp2`/`log2`; no angle computed only to take its cosine.
- **S** Repeated division by one value becomes a reciprocal (the rounding moves; name it).
- **S** Integer division by a power of two becomes a shift or mask; a specialization constant makes a divisor constant.
- **T** 16-bit values save registers in pairs, not time on Ampere+; `mediump` is refused.
- **T** `[[unroll]]` for small fixed counts, `[[dont_unroll]]` for large bodies.

## Memory and hoisting

- **S** Each row, texel or table entry is read once per invocation; hoist a second read.
- **S** A frame constant computed per pixel moves to the host's frame record.
- **S** A material or instance constant computed per hit moves to its row at load.
- **S** An expensive integral becomes a startup table built with the shared functions.
- **S** `textureGather` for four single-channel texels; `texelFetch` for node reads.
