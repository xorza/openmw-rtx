# Open issues

- **Two runs on the Windows machine's AMD integrated GPU no longer draw one picture**: `omw repeat --pairs=1` at
  `889fc79425` finds the trace differing on all 360 frames of `one-cell-walk`, from frame 1 and in every channel,
  `g-surface` and `g-albedo` among them; the gates of 16 and 17 November there found the pair identical.
