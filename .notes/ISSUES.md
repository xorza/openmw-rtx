# Open issues

- **The crash matrix's `fast-fail` case writes no dump on Windows**: on the Windows machine's AMD integrated GPU, at
  `889fc79425` with an accumulator change on top that touches neither, `crash.matrix` fails `fast-fail` with "the
  summary names none of the exceptions this mode raises: Crash: the game crashed, and no dump of it could be written";
  every other case passes.
- **Two runs on the Windows machine's AMD integrated GPU no longer draw one picture**: `omw repeat --pairs=1` at
  `889fc79425` finds the trace differing on all 360 frames of `one-cell-walk`, from frame 1 and in every channel,
  `g-surface` and `g-albedo` among them; the gates of 16 and 17 November there found the pair identical.
