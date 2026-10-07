# Questions on `redesign.md`

## 3. Section 8.1: the camera that spins

**Item.** Section 8.1, the camera that sometimes turns fast in the game.

**Why it needs a call.** The diagnostic lines are in the code at `Debug::Warning`, with TODOs to remove
them. The fix waits on a log of a spin from your game. Run the game, wait for a spin, and send
`openmw.log`'s `Mouse diagnostic:` lines from around it.

**What it blocks.** Section 8.1's fix and the removal of the diagnostic lines.

## 4. Section 6.1: the constellations drawn turned and squashed

**Item.** Section 6.1's "The constellations are drawn turned and squashed". The plan's target is an
affine fit of each patch's sheet axes, as `fitSheet` does for the cloud cap.

**Why it needs a call.** The vanilla patches are not flat and their sheets are not affine. Measured
on `sky_night_02.nif` (2026-10-07), each patch lies on the dome, at a radius of 1870 to 2000. The
best affine fit of the sheet coordinates still misses a vertex by this much of the sheet:

| Patch | Vertices | Orthographic fit | Central fit |
|---|---|---|---|
| mage | 7 | 0.012 | 0.015 |
| warrior | 12 | 0.34 | 0.37 |
| thief | 6 | 0.10 | 0.11 |
| nebula 02 | 20 | 0.26 | 0.30 |
| nebula2 02 | 28 | 0.29 | 0.38 |
| nebula3 02 | 15 | 0.24 | 0.27 |

The meshes are unwrapped triangle by triangle, so only their triangles say where a sheet lands.

| Option | What it does | Cost |
|---|---|---|
| **A. Bake each patch's sheet coordinates** (my pick) | At load, the reader finds, for each texel of a small grid over the patch's disc, the triangle the direction crosses and its sheet coordinates. The shader reads that map, then the sheet. The outline is the mesh's, too. | A data texture made at load, which the texture table has no kind for yet (the sprite light bake is the nearest), and one more fetch a patch on a ray that meets one. |
| B. Affine fit (the plan's target) | Fits each patch's axes and offset, and stores them in the patch record. | Leaves the errors above: four of six patches stay visibly wrong. |
| C. The triangles in a buffer | The miss shader finds the triangle a ray crosses, per patch. | About a hundred triangles, looped over on every sky pixel a patch covers. |

**What it blocks.** Only this item.

## 5. Section 6.1: the twin fold ignores the attributes it throws away

**Item.** Section 6.1's "The twin fold ignores the attributes it throws away". The plan's target is
a twin folded only where its corners carry equal coordinates and colours, and a mesh with a kept
twin flagged so that a ray takes the face its winding faces.

**What the count found** (2026-10-07, eight places: Balmora, Vivec, the ship, Ald-ruhn, Sadrith
Mora, the guild, Arkngthand, Mournhold). 81,111 twin pairs had equal corners. 101 mesh reads held
593 pairs with a differing one: 185 differ in coordinates and 81 in colour — a bench, rocks, tents,
the skiff, railings, the Vivec waterspouts. A back face of these shows the front's mapping.

**Why the target does not hold.** A ray that carries light meets both faces of everything
(`facingFor`), so a kept twin is met twice at one depth, and a cutout's or a pane's light is taken
twice. An instance can only switch culling off (`TRIANGLE_FACING_CULL_DISABLE`), never on, so no
flag on one mesh makes every ray take one face.

| Option | What it does | Cost |
|---|---|---|
| **A. A back face per kept triangle** (my pick) | The fold keeps one triangle as now, and records the dropped twin's corners where they differ. A mesh with any such twin carries a second index buffer; a hit on the back face reads its corners from it, by a select of the address and no branch. | The fold's output, the mesh table, the device upload, the mesh record and every shader that reads a triangle's corners. A second index buffer only for the meshes that need it. |
| B. Leave it | The back face of 0.7% of twin pairs shows the front's mapping. | The defect stays. |

The pocket rule (`dropPockets`) has the same blind spot, and option A answers it the same way: the
dropped wall's corners become the kept wall's back face.

**What it blocks.** Only this item.

## 7. Section 6.5: the upscaler's 64-lane subgroups

**Item.** Section 6.5's "FSR runs in full floats with the driver's wave size", the wave half. The
half-float half is done: measured and declined, and `fsrcallbacks.glsl` says why.

**Why it needs a call.** The SDK's own host asks for its 64-lane permutations on a device that runs
both 32 and 64 lanes, which RDNA does. NVIDIA runs 32 lanes only, so this card cannot take the
request, and the drm-shim device compiles every kernel and runs none. Section 6 keeps a `[perf]`
item only with a measurement, and no device here can make one.

| Option | What it does | Cost |
|---|---|---|
| **A. Wait for a measurement on RDNA** (my pick) | The item stays open until someone runs the default suite on an RDNA 2 or later card, with and without `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` at 64 on the upscaler's pipelines. | Nothing now. |
| B. Ask for 64 lanes as the SDK does, unmeasured | The upscaler's pipelines ask for 64 lanes where `subgroupSizeControl` allows it for compute. | A change no device here runs, kept on the SDK's word. |
| C. Drop the item | The driver keeps its own choice. | A possible gain on AMD goes unmeasured. |

**What it blocks.** Only this item.

## 8. Section 6.6: a translucent surface's shadow is one ray's

**Item.** Section 6.6's "A translucent surface's shadow sums are one ray's". Split, `gather` writes
every source's light times the drawn ray's `mThrough` into `CHANNEL_SHADOWED.rgb`, and the open or
shut bit into `a`. `gbuffer.h` says the `rgb` is exact per pixel. It is not where the ray crossed a
translucent surface: `mThrough` is that one ray's, and it changes with the cone draw every frame.

**Why it needs a call.** The shadow denoiser filters one bit a pixel and nothing else: `shadowmask.comp`
packs the bits, and the temporal pass and the levels read the packed words, never the float. So
`mThrough` cannot join the bit as a fraction, and each fix changes what the denoiser takes.

| Option | What it does | Cost |
|---|---|---|
| **A. Draw the translucency into the bit** (my pick) | The bit is open where the ray got through and a draw falls under `mThrough`, and `rgb` is the unshadowed light, exact. The bit's mean is `mOpen · mThrough`, which is what the product was, so the estimate stays unbiased, and the denoiser filters the translucency as it filters a penumbra. A pixel shut by the draw needs a reach for the levels to run (`CHANNEL_PENUMBRA`). | One draw per split pixel from a hash, so no other draw moves. The denoiser sees noise under glass that it does not see now. |
| B. Filter `mThrough` beside the bit, as SIGMA's translucent mode does | A second signal through the temporal pass and the levels. | A channel, its history and its filter: the largest change. |
| C. Keep it, and say so | `gbuffer.h` says `rgb` is exact except for what translucent surfaces let through, which is one ray's. | The noise under translucent surfaces stays. |

**What it blocks.** Only this item.

## 9. Section 6.6: the glossy filter's second history

**Item.** Section 6.6's "The glossy filter has no virtual-motion history". `specular.comp` keeps
one history, reprojected by the surface's motion and held by ReLAX's rule for how far the view
turned. A sharp lobe therefore starts again on every frame the eye turns. ReLAX keeps a second
history, reprojected along the reflected ray by the reflection's own parallax, and that needs the
lobe's hit distance in a channel.

**Why it needs a call.** It is a channel and a pass of new work, and the gain is for PBR replacers
alone: no vanilla surface has a lobe, so the filter does not run on any place the suites hold, and
nothing here can measure the gain or the cost.

| Option | What it does | Cost |
|---|---|---|
| **A. Wait for replacer content in the suites** (my pick) | A view with a mapped replacer goes into `views.cfg`, then the second history is built and measured there with `noise --strafe`. | The sharp reflections stay noisy in motion until then. |
| B. Build it now | The trace stores the lobe's hit distance (one `r16f` channel), and the glossy filter takes both histories and keeps the one that holds, as ReLAX does. | A channel and its history, unmeasured. |
| C. Decline it | The upscaler's accumulation stays the only help for a sharp lobe in motion. | The noise stays. |

**What it blocks.** Only this item.

## 10. Section 6.10: the fork's hunks the Accepted diff does not cover

**Item.** Section 6.10. Each hunk below is the fork's and no Accepted-diff entry names it, so by
AGENTS.md's rule it is either written into the Accepted diff with its reason or reverted. That
section of AGENTS.md is the user's policy, so the call is the user's.

| Hunk | Why it is there | My pick |
|---|---|---|
| MSVC's `4244` and `4267` off for the whole tree (`CMakeLists.txt`) | GCC's `-Wall -Wextra` leave `-Wconversion` out, so `/W4` held MSVC builds alone to narrowing rules no other compiler checks; off, every compiler checks one set, the rule the five added checks already state. | **Accept**, under the five checks' entry: "one set of checks for every file, on every compiler". |
| The build floor and toolchain: CMake 3.31, Boost 1.83, `CMAKE_CXX_SCAN_FOR_MODULES OFF`, the ccache fallback, `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`, the `$<COMPILE_LANGUAGE:C,CXX>` wrapping | 3.31 reads `CMakePresets.json`'s `$comment`; 1.83 is the Boost the flat maps of the scene identities need (`77cde9132a`); the scan preprocessed every file twice for modules the tree has none of; a runner with no ccache builds without it; ccache cannot cache a compile that writes a shared PDB; and Crashpad's MASM sources took the C++ flags. | **Accept**, as one entry: "the build the fork's presets and its Crashpad need". |
| `install_fork_licenses` and `files/licenses/*` | The fork ships Crashpad, VMA, FidelityFX and the Vulkan loader, whose licences ask to be shipped with them. | **Accept**: "the licences of what the fork ships". |
| The fork's workflows in place of upstream's four, and the root tooling (`CMakePresets.json`, `.zed/`, `omw`, `omw.cmd`, `.claude/skills/`, `.gitattributes`, `.gitignore`) | The CI the fork runs and the driver every verification step in AGENTS.md goes through. | **Accept**, beside `CI/`: "the fork's workflows and its driver". |

Revert is the other option for each row: the hunk goes, with what depends on it.

**What it blocks.** Only section 6.10.

## 11. Section 6.3: the WER module for Windows' fail-fast crashes

**Item.** Section 6.3's "Windows fail-fast crashes get no report". A `/GS` failure, a heap
corruption or a `__fastfail` ends the process past every filter inside it, so Crashpad's handler
never sees it. The monitor now logs the exit code such an end leaves (the half that needs no call).
What would take a dump is Crashpad's `crashpad_wer.dll`, which the Windows Error Reporting service
loads outside the game; Crashpad's CMake build already has the target.

**Why it needs a call.** Windows calls the module only where a registry value names its full path,
under `Software\Microsoft\Windows\Windows Error Reporting\RuntimeExceptionHelperModules`, in
`HKEY_CURRENT_USER` or `HKEY_LOCAL_MACHINE` ("WER Settings", Microsoft Learn). The fork ships a
portable archive and no installer, so the game itself would write the value into the player's
registry, where it outlives a moved or deleted copy of the game.

| Option | What it does | Cost |
|---|---|---|
| **A. The game registers itself under the player's key** (my pick) | At start, the Windows client writes the value under `HKEY_CURRENT_USER` for the DLL beside the executable, removes a value of its own name that points elsewhere, and calls `RegisterWerModule`; the package ships the DLL. | A registry value a player did not ask for, one per place the game ran from. |
| B. Only an installer registers it | The DLL ships, and only a future installer writes the value. | No dump until there is an installer. |
| C. No WER module | The exit code line is what a fail-fast leaves. | No dump for these crashes. |

**What it blocks.** Only the WER half of this item.

## 12. Section 6.3: the crash monitor after an AppImage's game is gone

**Item.** Section 6.3's "The monitor runs from the AppImage's mount after the game is gone". The
monitor is the game's own executable, started from the AppImage's mount, and it packages the
session and shows the dialog after the game exits, when the AppImage's runtime may have unmounted
the image. A file the monitor opens only then — SDL's video driver, which it loads for the dialog —
may not open.

**Why it needs you.** The check is a crash of the packaged game on your desktop, with its window and
a dialog that waits for a click, and AGENTS.md keeps the game window out of what I run.

**The check.** `./omw archive appimage-check` builds the AppImage into `dist/`. Then:

```
OPENMW_CRASH_DIALOG=1 ./dist/<the AppImage> &
sleep 20; kill -SEGV $(pgrep -x openmw)
```

The bug stands if no dialog shows, or the log under the crash folder ends without the package's
line. Send me the log's last lines either way.

| Option | What it does | Cost |
|---|---|---|
| **A. You run the check, then I fix what it shows** (my pick) | If confirmed, the monitor holds the mount: the AppRun keeps a file of the image open until the monitor ends. | One run on your desktop. |
| B. Fix it unverified | The same change, kept on the reasoning above. | A change no run here shows is needed. |

**What it blocks.** Only this item.
