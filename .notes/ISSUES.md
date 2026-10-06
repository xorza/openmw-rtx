# Open issues

- `rtx-gpu-tests` shard 0 (seed 14691) crashed once under `./omw gate` with SIGSEGV (SEGV_MAPERR) in a `libnvidia-glcore.so.615.71.09` worker thread, during the global setup, while other threads were in `Rtx::makeTracePipeline` under the validation layer (core of PID 76775, 2026-10-07 01:28). Fifteen reruns of the shard, warm, with `__GL_SHADER_DISK_CACHE=0`, and beside shard 1, passed.
