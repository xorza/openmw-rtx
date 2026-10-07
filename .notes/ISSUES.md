# Open issues

- `rtx-gpu-tests` shard 0 (seed 14691) crashed once under `./omw gate` with SIGSEGV (SEGV_MAPERR) in a `libnvidia-glcore.so.615.71.09` worker thread, during the global setup, while other threads were in `Rtx::makeTracePipeline` under the validation layer (core of PID 76775, 2026-10-07 01:28). Fifteen reruns of the shard, warm, with `__GL_SHADER_DISK_CACHE=0`, and beside shard 1, passed.
- Code comments cite the labels of `.notes/redesign.md` — `(D2 point 5)` in `historyclamp.glsl`, `(D3.2)` and `the D6 contract` in `shading.glsl`, `(D7)` in `fog.glsl`, `(D6)` and `(D2's registration)` in the pane and specular tests — and the plan is a working note that goes when its items are done.
