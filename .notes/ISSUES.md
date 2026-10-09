# Open issues

## The history clamp moves a few pixels far for an input change of one half step

With the glossy mean rounded at random to halves, a still PBR `shot --exposure=1` moves one to five
pixels of a 1920×1080 picture by 2 to 21 of 255 against the same tree with the mean in floats
(`seyda-neen-ship-west`, 21; `ald-ruhn`, 10); every other moved pixel moves by 1. With
`--antilag=0` on both sides, no pixel moves by more than 1. Under the measured exposure, the same
change moves `ahemmusa-yurt` by up to 53.
