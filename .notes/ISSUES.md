# Open issues

- **The shader-compile bar stops one report short**: `RtxRenderer::awaitShaders` reports `progress.mMade` before
  each wait and leaves the loop once the kernels are done, so the count the bar last shows is the one before the
  last kernel was made.
- **A failed NVML prime counts every stale sample**: `Nvml`'s constructor returns early only for `NotSupported`;
  any other failure of the first `fetch` leaves `mCursor` at 0, so the first `readSamples` counts every process's
  latest sample, whenever it was taken, as drawing in the run.
- **A film can keep an earlier film's frames**: `clearFrames` (`apps/rtxtool/model/film.cpp`) reads a failed
  `Platform::listFolder` as an empty folder, so the frames a longer earlier film left stay beside the new ones.
- **A refused counter ioctl reads as a thread never counted**: `ThreadCounters::start` and `stop`
  (`threadcounterslinux.cpp`) ignore the results of `PERF_EVENT_IOC_RESET`, `_ENABLE` and `_DISABLE`, so a kernel
  that refused them yields zero counts, reported as a thread the processor never ran.
- **The monitor's log can lose entries without a word**: `MonitorLog::finish` discards the result of opening the
  fallback file, and its five-second wait for the writer ends the same whether or not every entry was written.
