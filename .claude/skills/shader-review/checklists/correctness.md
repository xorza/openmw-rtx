# Correctness

## Undefined results

- `sqrt`, `inversesqrt`, `log` of a value that rounds below zero (`1 - x*x` of a unit dot): `max(fma(-x, x, 1), 0)`.
- `asin`/`acos` outside [-1, 1]; `pow(x, y)` with x < 0, or x = 0 and y ≤ 0; `atan(0, 0)`.
- `normalize` of a zero vector: degenerate triangle, parallel cross product, `l = -v`, a vertex with no tangent.
- Division by what can reach zero (`n·v`, `n·l`, pdf, luminance, weight sum): prefer a form where it cancels; an epsilon states its reason.
- `0 * Inf` is NaN: a zero weight does not cancel an infinite term. Select instead.
- `smoothstep` with e0 ≥ e1; `clamp` with lo > hi.
- `uint` underflow; shift by ≥ 32; `%` on negative `int`; float to `int`/`uint` out of range.
- `packHalf2x16` above 65504 is Inf: a new half field needs a bound.
- Denormals are flushed. Nothing may rely on values below 2^-126.
- Scattered `isnan`/`isinf` hide bugs. Non-finite values are counted at boundaries under `COUNTING`.

## Precision

- Take differences before magnitudes: camera-relative positions, triangle edges.
- One form for a hit point (`rayAt`). A second form gives a second point.
- `1 - (n·h)^2` near one: use `|n × h|^2`.

## GLSL and Vulkan

- `nonuniformEXT` on every descriptor-array index that is not dynamically uniform, on the resource. Subgroup-uniform is not enough.
- Every read outside a fragment stage names its level (`textureLod`, `textureGrad`, `texelFetch`).
- `barrier()` only in uniform control flow: tail lanes reach it and mask their writes. Buffer or image data shared across lanes also needs `memoryBarrierBuffer/Image` and `coherent`.
- Tail lanes of a dispatch never write out of bounds.
- Specialization map entries match type and size (`bool` is a 4-byte `VkBool32`); `constant_id` unique per pipeline.
- Shared structs: same order and widths on both sides, host `static_assert` on size, `uint` for a buffer `bool`.
- Storage image `layout(format)` matches the view; never sRGB.
- sRGB decoded once: by the sampler for colour slots, never for `Data` slots.
- `restrict` only where no other name aliases the resource.

## Ray tracing

- Secondary rays leave from an offset origin, along the geometric normal, on the side they leave by. Every trace site applies one bias, once. Wächter–Binder scales it with the position's magnitude; the tree uses `SHADOW_BIAS`.
- A shadow ray ends at the sampled light point less the bias.
- Visibility rays: `TerminateOnFirstHit` (plus `SkipClosestHitShader` in a pipeline). `Opaque` where no cutout test is wanted. Face culling only for correctness (`facingFor`).
- Cull masks agree with `MASK_*` and `rayMaskOf`.
- Any-hit can run more than once per primitive, in no defined order: nothing it accumulates may depend on count or order.
- Ray queries: a non-opaque candidate needs `rayQueryConfirmIntersectionEXT`; candidate and committed getters differ.
- Normals leave object space through `transpose(mat3(gl_WorldToObjectEXT))`, unless uniform scale is proven and stated. A mirrored placement flips the front face.
- Barycentric weights `(1 - b.x - b.y, b.x, b.y)` follow the index order.
- Payload location literals match their declarations; `hitAttributeEXT` does not cross a function call.
- Motion: previous transform and previous pose (`mPreviousPoseBlocks`); one stated sign and unit.

## Estimators

- Each sample divides by the pdf of the generator that made it, in the same measure (area to solid angle: `d² / |cos θ_light|`).
- Cosine-weighted hemisphere: pdf `cos θ / π`, so Lambert's estimate is `albedo · L`.
- Two strategies that can make one sample need MIS weights summing to one; else the light is counted twice or dropped.
- Russian roulette divides by the survival probability.
- One random number per decision; hashes with few BigCrush failures (PCG3D, xxhash32).
- RIS/ReSTIR: `W = Σw / p̂(X)` with the same target; confidence capped (5–30, 20 to start); MIS weights with neighbours' pdfs; never pick neighbours by their stored sample; never redraw a rejected sample; reused visibility is the usual hidden bias.
- A firefly clamp is bias: one place, indirect only, stated.
- A light sampled directly is not collected again by the bounce.

## DLSS Ray Reconstruction inputs

- Colour: linear HDR, noisy, before tone mapping.
- Albedos: linear, written for every pixel; a sky pixel gets a defined value (the guide suggests 0.5). The colour is divided by the albedo the network multiplies back.
- Specular albedo is the lobe's directional albedo. The guide's "linear roughness" is perceptual (its fit squares it into α).
- Depth and motion from the same data; no jitter in the matrices.
- Independent samples: white noise, no dithering or shared pattern; at least 32 jitter positions.
- Reset at every cut.
