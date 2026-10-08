# Open issues

- `VisibilityPass`'s single kernels — fog depth, sprite composite and sprite emitters — read the
  sky's variant constants (`SPEC_HAS_SUN`, `SPEC_HAS_MOONS`, `SPEC_HAS_SEA`) and are made once at
  the whole sky, whatever variant the frame traces under.
