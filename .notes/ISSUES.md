# Open issues

- **Two runs on the Windows machine's AMD integrated GPU no longer draw one picture**: `omw repeat --pairs=1` at
  `889fc79425` finds the trace differing on all 360 frames of `one-cell-walk`, from frame 1 and in every channel,
  `g-surface` and `g-albedo` among them; the gates of 16 and 17 November there found the pair identical.

- **The crash monitor takes the game for ended on a kernel without pidfds**: below Linux 5.3 `pidfd_open` fails, the
  monitor's `Platform::ProcessHold` holds nothing, and `hasEnded()` answers yes from the start, so the hang watch counts
  the game gone while it runs and posts `HandlerOverdue` once `sLastResort` passes.
