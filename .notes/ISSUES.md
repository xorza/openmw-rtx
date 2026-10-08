# Open issues

## The material table grows on the frame path

`SceneBuffers::mMaterialTable` is never reserved, though `SlotTable::reserve` exists for "a table
that grows on the frame path". An arrival that pushes the materials past a copy's size makes the copy
again in `SlotTable::sync` (`outgrow`) and rewrites the whole table, on each frame slot in turn.

## The scene report leaves the index blocks out, and its comment says the structures count them

`SceneBuffers::getBytes` (`scenebuffers.cpp:459`) skips the indices because "they belong to the
acceleration structure, which reports its own size". `SceneAcceleration::getStructureBytes`
(`sceneacceleration.hpp:127`) counts the bottom and top levels alone, so no report counts the index
blocks.

## The shadow filter's history passes through `packHalf2x16`

`shadowfilter.comp:117` packs each tap's temporal value into shared memory with `packHalf2x16`, and the
first level's answer is the next frame's history. The spec leaves that conversion's rounding to the
device, so on a device that rounds toward nought the history is biased at every frame, whatever the
images' full-float formats.
