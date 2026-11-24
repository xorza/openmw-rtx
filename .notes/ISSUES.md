# Open issues

- **The desk's RADV drm-shim is older than the one the kernels' ceilings name**: `../mesa/build-shim` is Mesa 26.2.3
  (`31e9a6b`), and `sRadv` in `kernels.cpp` was compiled again by 26.2.7. Under it,
  `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` fails at master for `vangogh`, `raphael_mendocino` and
  `navi21`: `direct` and `direct sea` hold 112 registers against a ceiling of 96.
