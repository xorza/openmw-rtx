# Open issues

- `STAR_RADIANCE` in `components/rtx/shaders/look.h` (what a texel of the star field is worth as radiance) was set through a reconstruction the renderer no longer has, which took a fifth off a star; the level has not been measured again through the wavelet, so stars now come out about a quarter brighter than the content puts them.
- The drawn-and-divided indirect rate in `components/rtx/shaders/look.h` (the indirect term at nought or twice itself) was judged under a denoiser the renderer no longer has; it has not been judged under the wavelet.
- The `crash.matrix` CTest suite takes about 36 s, over the 30 s a suite is allowed.
