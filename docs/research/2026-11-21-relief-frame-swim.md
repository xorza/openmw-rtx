# Why a marched relief swam under a step aside — 2026-11-21

The PBR replacer profile, release builds, the high-end NVIDIA laptop GPU. The place is `balmora-relief`, the rock
beside the road up to Balmora's gate at noon, where a sideways step showed the stone flowing.

## The sideways leg

`noise --views=balmora-relief --strafe=300`: the frame's bias against the frames averaged, before the fix.

| run | shaded | albedo alone (`--show=albedo`) |
|---|---|---|
| FSR, relief marched | 1.56 | 1.33 |
| FSR, relief shifted (`--relief=0`) | 1.40 | 1.17 |
| FSR, no parallax at all | 1.01 | 0.94 |
| DLSS, relief marched | 0.71 | 0.60 |
| no upscaler, relief marched | 0.36 | 0.20 |
| vanilla, FSR | 0.53 | 0.48 |

The relief added half again to FSR's bias and nothing without an upscaler: the history fetched along the motion was
not the texel the frame showed. What did not move it, on the shaded leg: the eye's motion measured to the triangle
and not to the march (1.66), the march's distance handed to the upscaler as the triangle's (1.56), the relief in
FSR's transparency-and-composition mask (1.53), its grazing clamp taken out (albedo 1.34), a march four times finer
(no change to the error below). The relief in FSR's reactive mask took the bias to 0.91 and the frame's noise from
1.45 to 1.66: shimmer for smear. The relief read one level coarser took the albedo's to 1.20.

## The texel the motion promises

A diagnostic in the direct stage, for every pixel whose relief was marched: the point the eye's motion is measured
to, seen from an eye three units to the side, marched again from where that eye's ray to the point crosses the
plane; the distance between the texel that march shows and the one this frame shows, in texels of the level the
march read. A still shot, so the step is the same at every pixel; with no step the distance read under an eighth of
a texel everywhere.

| the march's frame | under 1/8 texel | 1/8–1/2 | 1/2–2 | 2–8 | 8 and over |
|---|---|---|---|---|---|
| the mesh's tangents, unit and square (shipped) | 49.7% | 29.6% | 17.5% | 2.8% | 0.4% |
| the sheet's gradients, up along the shading normal | 73.3% | 12.7% | 11.6% | 1.9% | 0.5% |
| the sheet's gradients, the height carried to the plane's square | 99.8% | 0.2% | 0.0% | 0.0% | 0.1% |

The second row's errors stood on whole triangles, those whose shading normal leaned off their plane: a height along
that normal, measured from a point on the plane, is another height from the next point. The third is `ReliefFrame`:
across and down the sheet by its own gradients, and up along the shading normal by a height over the plane. With it
a point of the relief is one point whichever ray finds it, which is what a motion measured to that point needs.

## After

The third frame in every march, the shift's and the ground stack's among them:

| run | shaded | albedo alone |
|---|---|---|
| FSR | 0.99 | 0.95 |
| DLSS | 0.70 | |
| no upscaler | 0.32 | |

FSR's bias with the relief is the bias with no parallax at all. `repeat --pairs=10` under the profile: identical. The
relief on this rock looks different from before, since it is now marched along the sheet its texture is laid on.

## What the kernels hold

Every pipeline's statistics before and after, at the debug log level: on the desk through
`RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`, on the Windows host with an AMD integrated GPU under AMD's
own driver through a `shot` over `omw remote`, each side compiled cold, the driver's shader cache set aside.

- **NVIDIA**: no stage's shared or local memory moved, and of the registers only `unbounced maps relief` (77 to 79)
  and `unbounced sea maps relief` (79 to 80). The binaries of the tuples with maps grew 1–2.5%.
- **AMD's driver**: no tuple with maps moved in vector or scalar registers, shared memory or scratch. `unbounced` and
  `unbounced sea` lost vector registers, 89 to 85 and 96 to 93, the same from one cold compile to another. Their
  modules specialized and optimized are what they were (`omw kernels`); the module the driver is handed is not
  specialized first, and its relief's dead code now holds the frame as a struct of its own.
