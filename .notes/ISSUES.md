# Open issues

- `./omw shot --views=all --map --upscale=off` is not deterministic with the denoiser on: on the
  same build, about half of the runs give a different `g-direct` hash (and sometimes a picture
  that differs by 1 in a few pixels) in two to seven views, a different set each run, while every
  trace channel and the scene digest stay the same. The one run taken at `--upscale=quality`
  gave no difference. `./omw repeat` does not see it, because it walks with the denoiser off.
