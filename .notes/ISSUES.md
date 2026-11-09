# Open issues

- `Rtx::parallel` (`components/rtx/common/parallel.hpp`) and the game's chain workers (`PreparedImages`, made in `RtxRenderer`'s constructor) count their threads from `std::thread::hardware_concurrency()`, which counts every CPU the system has and not those the process may run on: under the harness, held to the 16 CPUs of the performance cores, the chain workers are 16 threads on those 16.
- On a low-end AMD laptop under Windows, `components-tests` filtered to the preparation, platform, resource and image suites ended once with `0xC0000409` after all 84 tests passed (random seed 61386); the same filter passed and exited 0 seven more times, the same seed among them.
