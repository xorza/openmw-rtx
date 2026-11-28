# Open issues

- **The crash monitor takes the game for ended on a kernel without pidfds**: below Linux 5.3 `pidfd_open` fails, the
  monitor's `Platform::ProcessHold` holds nothing, and `hasEnded()` answers yes from the start, so the hang watch counts
  the game gone while it runs and posts `HandlerOverdue` once `sLastResort` passes.
