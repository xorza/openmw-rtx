# Open issues

## The material table grows on the frame path

`SceneBuffers::mMaterialTable` is never reserved, though `SlotTable::reserve` exists for "a table
that grows on the frame path". An arrival that pushes the materials past a copy's size makes the copy
again in `SlotTable::sync` (`outgrow`) and rewrites the whole table, on each frame slot in turn.

## The history clamp moves a few pixels far for an input change of one half step

With the glossy mean rounded at random to halves, a still PBR `shot --exposure=1` moves one to five
pixels of a 1920×1080 picture by 2 to 21 of 255 against the same tree with the mean in floats
(`seyda-neen-ship-west`, 21; `ald-ruhn`, 10); every other moved pixel moves by 1. With
`--antilag=0` on both sides, no pixel moves by more than 1. Under the measured exposure, the same
change moves `ahemmusa-yurt` by up to 53.

## A moving frame at the deck is noisier than its frames averaged

`./omw release noise --strafe=150 --walk=150` reports the frame noisier than 13 frames averaged at
every view of the Seyda Neen deck: `seyda-neen-ship` (p99 24.47 against 15.69),
`seyda-neen-ship-dawn` (19.16 against 16.53), `seyda-neen-ship-overcast` (15.72 against 10.91) and
`seyda-neen-ship-west` (24.66 against 15.12). Each still leg is as clean, and the strafe and the walk
each show it alone. The pixels far from the frames' mean are on the leaves against the sky. With
`--upscale=off` the moving dawn frame's p99 is 32.84 against 14.40. `--antilag=0`, `--history-fix=0`,
`--dual-motion=0` and `--antifirefly=1` leave it as it is, and so does the soft-edge dither turned
off, which raises the fireflies from 0.65 to 1.49 in a thousand.
