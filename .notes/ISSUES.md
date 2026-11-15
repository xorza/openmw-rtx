# Open issues

- `omw gate`'s clang-tidy step fails over the whole tree on Windows: 283 findings in 207 sources, mostly `misc-include-cleaner` naming `<GL>` and `bugprone-exception-escape`.
- Under the PBR profile the tour's process holds 12 GiB of anonymous memory by its end (6.7 GiB by its 34th second, measured on the desk) beside 5.7 GiB of device memory, which on a 16 GiB handheld is the same memory: there the device has no room for a texture and then a mesh at legs 3 and 4, and the system's `earlyoom` ends the game during leg 5 of 93 with a `SIGTERM`, which the log shows as `the game ended during stop 5` and a peaceful quit with no reason.
