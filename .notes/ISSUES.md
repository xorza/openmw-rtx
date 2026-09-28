# Open issues

- `components/rtxvulkan/upscale/dlss.cpp` includes the deprecated `nvsdk_ngx_helpers_dlssd.h` for `NGX_DLSSD_GET_OPTIMAL_SETTINGS`, which in NGX 310.9.1 exists only in the D3D and CUDA helper headers; every build prints the SDK's `#pragma message` deprecation note.
