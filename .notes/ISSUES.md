# Open issues

- `omw kernels` refuses on a box set up by `omw bootstrap`: it wants `spirv-dis` beside the build's
  `spirv-opt`, and the SDK `deps.py` fetches keeps only `glslc`, `spirv-val` and `spirv-opt`.
