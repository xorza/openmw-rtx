# AMD GPU support and swappable reconstructors

The renderer can run on every AMD GPU that can run this ray tracer: Radeon RX 6000 (RDNA 2) and
later, on Windows and on Linux. The renderer no longer has any upscaler: it traces at the window's
size, the wavelet denoises, and a neutral `Upscaler` seam waits for one. Three changes make AMD
possible:

1. The seam becomes a reconstructor abstraction with FSR 3.1 behind it.
2. AMD has no ML denoiser on Vulkan. With FSR, the in-tree wavelet denoises and FSR 3.1 upscales. NRD
   and XeSS are not options, because their licences conflict with the GPLv3 (section 8).
3. RADV, the Linux AMD driver, has no hit objects. The primary trace needs a second compile that uses
   `traceRayEXT`. AMD's Windows driver has hit objects, so Windows needs only items 1 and 2.

All research is from September 2026. The sources are at the end.

## 1. What stands in the way

### 1.1 Blockers

| # | Blocker | Where | Effect on AMD |
|---|---|---|---|
| B4 | One upscaler per binary, chosen at link time: `upscale/upscaler.cpp` defines the seam's free functions. | `upscale/upscaler.hpp`, `components/rtxvulkan/CMakeLists.txt` | FSR needs a registry beside the seam, not a second definition. |
| B5 | The primary trace keeps each hit in a `hitObjectEXT` (`RTX_TRACE`, `RTX_SHADE`, 20 calls). The payload leaves out the hit flag and the distance because the hit object holds them. | `shaders/trace/visibility.rgen`, `shaders/lib/payload.glsl`; required extension `VK_EXT_ray_tracing_invocation_reorder` | RADV does not offer the extension, and no Mesa merge request implements it. Linux AMD is refused. |
| B6 | Driver floors are NVIDIA versions only (`"595"`), and the refusal message names only the NVIDIA driver. | `device/requirements.cpp`, `physicaldevice.cpp` | An old AMD driver gets a message with no AMD version in it. |


### 1.2 NVIDIA-only features that are already optional

These do not block AMD. Each has an AMD counterpart for a later phase.

| Feature | NVIDIA | AMD Windows | RADV (Linux) |
|---|---|---|---|
| Crash breadcrumbs (`Device` checkpoints) | `VK_NV_device_diagnostic_checkpoints` | `VK_AMD_buffer_marker` | `VK_AMD_buffer_marker` |
| Harness card watch (`instruments/nvml.*`) | NVML | ADLX (not planned) | amdgpu sysfs and `fdinfo` |
| Harness driver cache (`instruments/drivercache.*`) | `__GL_SHADER_DISK_CACHE_*` | driver-managed | `MESA_SHADER_CACHE_DIR`, `MESA_SHADER_CACHE_MAX_SIZE` |

The card watch and the driver cache already report what they could not read, so they do not fail.

### 1.3 Checked and vendor-neutral

- The shaders use only `EXT` and `KHR` GLSL extensions, and no subgroup operations.
- The shader table reads `shaderGroupHandleSize`, `shaderGroupHandleAlignment` and `shaderGroupBaseAlignment` from the device.
- The memory kinds have a fallback without resizable BAR (`PhysicalDevice::Profile::mHostWrittenBytes`).
- GPU timers read `timestampPeriod` and `timestampValidBits`.
- The pipeline cache is keyed on `vendorID`, `deviceID` and `pipelineCacheUUID`.
- The pinned float arithmetic needs `VK_KHR_shader_fma`, which AMD now offers (table below). No other NVIDIA assumption is in `spirv/spirvpin.hpp`.
- The storage formats `R16` and `R8` are standard storage formats.

## 2. Driver support for the required extensions

| Required | NVIDIA | AMD Windows (Adrenalin) | RADV (Mesa) |
|---|---|---|---|
| acceleration structure, ray query, ray tracing pipeline, maintenance 1 | yes | yes | gfx10.3+ (RDNA 2+) |
| `VK_KHR_ray_tracing_position_fetch` | yes | 23.7.1+, RX 6000+ | gfx10.3+ |
| `VK_EXT_ray_tracing_invocation_reorder` | 595+ | **26.2.1+** (Feb 2026) | **no** |
| `VK_KHR_shader_fma` | 595+ | **26.3.1+** (Mar 2026) | **26.2+** (Aug 2026) |
| `VK_KHR_pipeline_executable_properties` | yes | 19.11.1+ | yes |
| `VK_KHR_shader_clock` | yes | 19.11.1+ | yes |
| Vulkan 1.4 (`pushDescriptor`, `maintenance5`) | yes | 25.5.1+ | yes |
| `VK_EXT_device_fault` (optional) | yes | unknown | yes |

To verify with the drm-shim (step 0): RADV's `rayTraversalPrimitiveCulling`, `shaderDeviceClock`, and
storage `R16`/`R8`. The same three on AMD Windows need a tester or a cloud GPU.

The minimum AMD hardware is RDNA 2 (RX 6000), because position fetch and RADV's ray tracing start
there. RDNA 2 and RDNA 3 traverse the BVH in shader code, so expect path tracing to be much slower
than on RTX. RDNA 4 (RX 9000) is the first AMD generation with strong ray tracing hardware.

## 3. What AMD offers for reconstruction on Vulkan

| Option | API | Denoises | Upscales | Licence | Use here |
|---|---|---|---|---|---|
| FSR 4 upscaling, FSR Ray Regeneration (FSR SDK 2.x) | DirectX 12 only, Windows 11 | yes (RR) | yes | AMD | **No.** No Vulkan backend, and AMD has not said one will come. |
| FSR 3.1 upscaler (FidelityFX SDK 1.1.4, May 2025) | Vulkan and DX12; GLSL passes for Vulkan | no | yes, analytic | MIT | **Yes.** Any vendor, NVIDIA included. |
| NRD ReBLUR / ReLAX | Vulkan, DX12 | yes | no | NVIDIA RTX SDKs licence | **No.** The licence conflicts with the GPLv3 (section 8). |
| XeSS 2.1 SR | Vulkan, DX12; any Shader Model 6.4 GPU | no | yes, ML | Intel Simplified Software Licence, binary only | **No.** No source, so the GPLv3 cannot be met (section 8). |
| In-tree wavelet (`AccumulatePass`, `AtrousPass`) | this backend | yes | no | this tree | Denoiser for FSR in the first version. |

FSR 3.1 only upscales, so a frame for FSR is first denoised by the wavelet at render size. The trace chain
already does this: when the filter runs, `TraceResult::mColour` is the denoised composite
(`trace/tracechain.cpp`), which FSR can read as its colour input. FSR 3.1 is MIT-licensed, and the
MIT licence is compatible with the GPLv3.

The SDK compiles the FSR 3.1 GLSL passes with `FidelityFX_SC`, a Windows-only tool. This plan ports the
upscaler component into the backend and compiles its passes with the tree's own `glslc`, pinning and
`spirv-val`, as Godot did with FSR 2.2. The port does not link the SDK's runtime.

## 4. Design

### 4.1 The core (`components/rtx/frame/`)

A reconstructor is described by data, and `Reconstruction::resolve` reads the data. The core never
asks which vendor made it.

```cpp
/// What a reconstructor is, as the frame rule needs to know it.
struct ReconstructorTraits
{
    ReconstructorId mId;              // Fsr, ... (a NamedEnum, one list of spellings)
    JitterSequence mJitter;           // the phase count rule the reconstructor asks for
    float mLevelBiasOffset = 0.0f;    // added to log2(render / output): -1 for FSR
};
```

- Done: `Denoiser` is `None` and `Wavelet`, `Upscaling` holds the mode
  alone, `Upscale::Native` is the 1:1 mode, and under an upscaler the wavelet runs, the frame
  jitters and the level bias follows the ratio.
- `sUpscalerBuilt` goes away. What is built and what is available become run-time answers from the
  backend (below).
- The texture level bias takes the trait's constant: FSR's guide gives `log2(render / output) - 1`.

### 4.2 The backend (`components/rtxvulkan/upscale/` becomes `reconstruct/`)

- `Upscaler` becomes `Reconstructor`: `getTraits`, `renderSizeFor`, `resize`, `release`, `getOutput`
  and `record(const ReconstructInputs&)`.
- `ReconstructInputs` grows from `UpscaleInputs`: the camera's near and far planes and vertical field
  of view, the pre-exposure, and an optional reactive mask. Each implementation reads what it needs.
  The G-buffer holds the colour and the motion that FSR asks for. It holds the distance along each
  ray and not a device depth, so the FSR port derives the depth from the distance, the near and far
  planes and the pixel's ray in its own input pass, on the frames it upscales.
- A registry replaces the link-time choice:

  ```cpp
  struct ReconstructorFactory
  {
      ReconstructorId mId;
      std::span<const char* const> (*mInstanceExtensions)();
      std::span<const char* const> (*mDeviceExtensions)();
      Availability (*mProbe)(const Device&, VkInstance);   // available, or why not
      std::unique_ptr<Reconstructor> (*mMake)(const Device&, VkInstance);
  };
  ```

  `reconstruct/registry.cpp` lists `fsrFactory()`, and a later reconstructor joins the list.
- The instance and the device ask for each factory's extensions as optional: enabled when offered.
  A factory whose extensions are not all offered probes as unavailable, with the missing names as the
  reason. `profileOf` never refuses a device for a reconstructor.
- `upscale/upscalerextensions.hpp` goes away. Its two declarations become the factory's two fields.
- `Renderer` (the core seam) gains `describeReconstructors()`: each built reconstructor, with
  "available" or the reason not. The settings window lists the available ones. The launcher has no
  device, so it lists the built ones.

### 4.3 Selection and settings

- `[RTX] upscale` is the quality of the one upscaler: `off`, `ultraperformance`, `performance`,
  `balanced`, `quality`, `native`. A key that chooses between upscalers comes with a second one.
- Settings the renderer no longer has are not read.
- An upscaler that the GPU cannot run leaves the renderer at `off`, logs why, and writes the setting
  back: the rule of `RtxRenderer::setUpscale`.
- Harness: `sUpscaleByDefault` and `sFilmUpscale` in `apps/rtxtool/run.hpp` read the registry, not a
  build constant. The bench record stores the upscaler.

### 4.4 FSR 3.1 in the backend (`reconstruct/fsr/`)

- Source: FidelityFX SDK v1.1.4, `sdk/include/FidelityFX/gpu/fsr3upscaler/*.h` and
  `sdk/src/backends/vk/shaders/fsr3upscaler/*.glsl`, vendored under `extern/fsr3upscaler/` with the MIT
  licence text. `extern/` is where the tree keeps compiled third-party source.
- Passes (9 compute): prepare inputs, luma pyramid, shading change pyramid, shading change, prepare
  reactivity, luma instability, accumulate, RCAS, and the debug view (debug builds only).
- Host: a port of `ffx_fsr3upscaler.cpp`: the constant blocks, the resource sizes, the jitter phase
  count (`ffxFsr3UpscalerGetJitterPhaseCount`), and the fixed ratios: 1.5 for quality, 1.7 balanced,
  2.0 performance, 3.0 ultra performance, 1.0 native.
- Every FSR module goes through `openmw-rtx-spirv-pin`, so FSR frames are the same on every compile,
  and `omw repeat` holds. Compile with `FFX_HALF=0` first. If the pinning refuses an operation, the
  build names it.
- The puffs and the display chain already come after the reconstruction, so FSR's composition mask
  starts empty. A reactive mask for the water and the fog is a second step, measured with `omw shot`.

### 4.5 The trace without hit objects (fixes B5)

- `visibility.rgen` compiles twice: `visibility.rgen.spv` with hit objects, and
  `visibility-traced.rgen.spv` with `-DRTX_HIT_OBJECTS=0`. A specialization constant cannot do this,
  because a module that names a hit object declares the capability even on a path that never runs.
- In the second module, `RTX_TRACE` and `RTX_SHADE` use `traceRayEXT`. The closest-hit and miss
  shaders do not change.
- The payload gains a hit flag and the hit distance in that module only. The ray's origin and
  direction stay in local variables in the launch.
- "Trace, and shade only on a hit" (the arms, and the peel through the arms): trace with
  `gl_RayFlagsSkipClosestHitShaderEXT`. The miss shader clears the hit flag. Shade with a second
  `traceRayEXT` only where it hit. That is one more traversal on those pixels only. A ray query in
  the launch is the alternative, and the bench decides between the two.
- `VK_EXT_ray_tracing_invocation_reorder` becomes a `DeviceOption`. Without it, `Reorder` resolves to
  `None`, and the traced module is chosen. The probe test `traceprobe.rgen` keeps the extension.
- Cost: the any-hit shader sees the larger payload in the traced module. The launch count doubles
  only in `omw kernels`, not per frame.

### 4.6 Breadcrumbs, driver floors

- Breadcrumbs: `VK_AMD_buffer_marker` beside the NVIDIA checkpoints, written at the same points, and
  read after a lost device.
- `RequiredExtension::mNvidiaDriver` becomes a table keyed by `VkDriverId`: NVIDIA `595`, AMD
  proprietary `Adrenalin 26.3.1`, RADV `Mesa 26.2`. The refusal names the floor of the driver it
  found.

### 4.7 The harness

- `CardWatch` reads a `CardSource`: `NvmlSource` (now), and `AmdgpuSysfsSource` on Linux: the clock
  from `hwmon/freq1_input`, the temperature from `hwmon/temp*_input`, and the per-process share from
  `/proc/<pid>/fdinfo` `drm-engine-gfx`.
- `DriverCache` also sets `MESA_SHADER_CACHE_DIR` and `MESA_SHADER_CACHE_MAX_SIZE`. It sets nothing
  for AMD Windows.
- `omw kernels` and `omw repeat` are vendor-neutral already.

## 5. Tests without AMD hardware

1. **RADV through Mesa's drm-shim.** `libamdgpu_noop_drm_shim.so` pretends to be an AMD GPU, and RADV
   then creates a device that compiles but runs nothing. It can expose `NAVI21` (RDNA 2), `NAVI31`
   (RDNA 3) and `GFX1201` (RDNA 4). Arch's `mesa` package does not ship it. Build it from the Mesa tag
   that matches the installed RADV (26.2.3), with `-Dtools=drm-shim`. With it:
   - `LD_PRELOAD=…/libamdgpu_noop_drm_shim.so AMDGPU_GPU_ID=navi31 vulkaninfo` gives RADV's real
     extensions and features for each generation. These become test fixtures for `profileOf`.
   - A new harness verb, `compile`, selects a device by name, makes every pipeline and prints the
     registers and spills from `VK_KHR_pipeline_executable_properties`. That is a compile and
     occupancy check for RDNA 2, 3 and 4 in CI.
2. **FSR on the RTX 4090.** FSR 3.1 runs on any vendor, so all of its development and its device
   tests run here: the render size for each ratio (exact, from the fixed ratios), a flat frame that
   resolves to itself, and `omw repeat`.
3. **The traced module on the RTX 4090.** A harness switch forces the module without hit objects on
   NVIDIA. The two modules run the same shaders with the same pinned arithmetic, so
   `omw shot --against` must show no change. That is a strong check of phase 5, on this machine.
4. **Device selection.** `RtxPhysicalDeviceTest` already judges `profileOf` from an extension list. A
   case with the AMD fixtures must select the device, with FSR available.
5. **Real AMD hardware** is needed only for the last check: frame times, and the three features
   marked for verification in section 2. The options are a community tester, or a cloud GPU with an
   RDNA 2 Radeon Pro V620 on Windows (Azure NGads V620).

## 6. Implementation plan

Each phase ends green on `./omw test` and `./omw gate`, and changes no picture on NVIDIA unless the
phase says so.

| Phase | Work | Tests | Result |
|---|---|---|---|
| 0 | Build the drm-shim in `~/Projects/mesa` (Mesa 26.2.3, not installed; used by `LD_PRELOAD` from its build folder). Record `vulkaninfo` for navi21, navi31 and gfx1201 as fixtures. Add a device selector (`--device=` and `[RTX] device`) to the harness and the renderer. | fixtures committed | RADV's facts are data, not memory. |
| 1 | Driver floors per `VkDriverId` (B6). | `profileOf` with the AMD fixtures; a refusal names the AMD floor | AMD Windows runs, with the wavelet. |
| 2 | The reconstructor abstraction: core traits in `resolve`, the backend registry (B4), the reports. | `resolve` table-driven over traits; a fake factory in the registry; `omw shot --against` identical on NVIDIA | Swappable reconstructors. |
| 3 | FSR 3.1 upscaler, ported, and the menus offer its modes. | render sizes per ratio; flat frame; `omw repeat`; `omw kernels` lists the new kernels | AMD Windows plays with wavelet and FSR. |
| 4 | The trace without hit objects (B5). | `omw shot --against` identical with the switch forced on NVIDIA; drm-shim compile on navi21, navi31, gfx1201 | Linux AMD (RADV) runs. |
| 5 | Buffer-marker breadcrumbs, the amdgpu card source, the Mesa cache variables. | unit tests of each source's parsing | Parity of the optional features. |
| 6 | Policy and documents: `AGENTS.md` target hardware, `README.md`, `docs/rtx/architecture.md`, `rtx.rst`, settings-default comments, translations. | `RtxSourceTreeTest` document paths | The tree says what it supports. |
| 7 | Improve the in-tree denoiser for upscale-only reconstructors (the wavelet and the temporal accumulator), measured with `omw shot` and `omw bench`. NRD and XeSS are out (section 8). | as phase 3 | Better AMD picture quality. |

The order matters: phase 1 alone lets AMD Windows run, and phase 2 must come before phase 3.

## 7. Decisions

- **D1. Target and platform:** every AMD GPU that can run this ray tracer with the plan done. That is
  RDNA 2 and later, on Windows and on Linux, so phase 4 is in scope. `AGENTS.md` changes in phase 6.
- **D2. NRD:** out, because of the licence conflict in section 8. The same applies to XeSS.
- **D3. Settings:** settings the renderer no longer has are not read; `[RTX] upscale` is the one upscaler's quality
  (section 4.3).
- **D4. FSR integration:** a port into the backend (section 4.4).
- **D5. drm-shim:** built from Mesa 26.2.3 in `~/Projects/mesa`, and not installed.

## 8. Licences

The fork is GPLv3 (`LICENSE`). Each third-party part that a shipped binary contains must be
compatible with it.

| Part | Licence | In a shipped binary | Compatible with GPLv3 |
|---|---|---|---|
| FSR 3.1 upscaler (planned) | MIT | yes, as source compiled in | yes; keep the MIT notice |
| Vulkan Memory Allocator | MIT | yes | yes |
| Crashpad | Apache 2.0 | yes | yes (Apache 2.0 is compatible with GPLv3, not with GPLv2) |
| Mesa drm-shim (planned) | MIT | no, a test tool | not relevant |
| NRD | NVIDIA RTX SDKs licence | would be | **no** |
| XeSS | Intel Simplified Software Licence, binary only | would be | **no** |

Why NRD and XeSS conflict:

- **The GPLv3** requires that a distributed program, and every part that forms one combined work with
  it, comes with its Corresponding Source under the GPLv3 (sections 5 and 6). A proprietary library
  linked into the program is part of the combined work. The system-library exception does not cover
  an SDK that the program ships.
- **The NVIDIA RTX SDKs licence**, section 2(e), says: "You may not use the SDK in any manner that
  would cause it to become subject to an open source software license", with the examples of a
  licence that requires source form or free redistribution. The GPLv3 requires both.
- **XeSS** ships as binaries only and forbids modification, so its source cannot be given.

So a binary that links an SDK under that licence cannot meet both licences at once. This is a question
for a lawyer, not a settled answer, but the text of both licences points the same way, and the tree
links no such SDK. This makes the reconstructor abstraction and FSR (phases 2 and 3) the path to an upscaled
picture on every GPU, NVIDIA included.

## Sources

- Mesa feature list (RADV extensions per generation): https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/features.txt
- RADV adds `VK_KHR_shader_fma` (Mesa 26.2): https://www.phoronix.com/news/RADV-VK_KHR_shader_fma
- Mesa 26.2.0 release notes (2026-08-05): https://docs.mesa3d.org/relnotes/26.2.0.html
- AMD Windows Vulkan driver support history: https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-VULKAN.html
- `VK_EXT_ray_tracing_invocation_reorder`: https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_ray_tracing_invocation_reorder.html
- Mesa AMD drm-shim: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/amd/drm-shim
- FSR Ray Regeneration (DirectX 12, Windows 11, RX 9000): https://gpuopen.com/amd-fsr-rayregeneration/
- FSR SDK 2.3 (DirectX 12 only): https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/
- FidelityFX SDK (FSR 3.1 Vulkan, MIT): https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- FSR 3.1 with Vulkan: https://gpuopen.com/learn/amd_fsr_3_1_release/
- NRD and its licence: https://github.com/NVIDIA-RTX/NRD
- XeSS SDK 2.1 and its licence: https://github.com/intel/xess
- GPLv3: https://www.gnu.org/licenses/gpl-3.0.html
- GPL FAQ (separate programs, plugins): https://www.gnu.org/licenses/gpl-faq.html
