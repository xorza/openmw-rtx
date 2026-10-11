# Open issues plan: questions

## Item 3: how to test a surface made again

**What is blocked.** Item 3, a lost surface gets a new surface. The code is done and builds:
`.notes/open-issues-plan_item3.patch` holds it, with its test, against the commit that adds this
file (`git apply .notes/open-issues-plan_item3.patch`). Nothing else waits on it.

**Why.** The item's "Close first" closed the other way. The test made its window on SDL's offscreen
driver, whose surface comes from `VK_EXT_headless_surface`. This card's driver lists that instance
extension, but no queue family presents to such a surface:

```
no Vulkan device meets this renderer's requirements:
  NVIDIA GeForce RTX 4090 Laptop GPU: no queue family with graphics and compute that presents to the window
```

So no swapchain stands on a surface that needs no display, and the presenter's rebuild on a new
surface has no test that runs without one.

**Options.**

1. **A real window on the desk's display (recommended).** The test makes a hidden window on the
   Wayland session, as `RtxWindow` does, and runs the patch's test unchanged on it. The GPU binary
   already runs on this box alone. Cost: the test needs `WAYLAND_DISPLAY` (or `DISPLAY`) in its
   environment. The shell this plan ran in had none, so the test must fail with that reason where
   no display is reachable, as the binary fails without a device, and `./omw` must pass the
   session's display through.
2. **The bookkeeping alone, without Vulkan.** The lost and remade states become a small class that a
   `components-tests` test drives. The Vulkan path — the swapchain released, the surface made again,
   the swapchain renewed — then has no automatic test, and is checked once by hand.
3. **No automatic test.** Commit the patch without its test, and check the path once by hand.

**Recommendation.** Option 1: it tests the real path, on the one machine that runs the GPU binary.

**Whichever option.** The patch's test is a plain `TEST` in `rtx-gpu-tests`, which
`RtxSourceTreeTest.everyTestIsInTheBinaryItsNeedsDecide` refuses: it must run over a device fixture.

## Item 5, done: a settings file from before the change

**What is open.** Item 5 is committed without the one-time conversion that the plan's review added
to Decision 2. Nothing is blocked: the game behaves as Decision 2 accepted.

**Why.** The settings migration runs when the settings load, before SDL starts, so it cannot read a
display's density. `openWindow` has the density but no word of which file the size came from. A
conversion needs a channel between the two, and none exists.

**Options.**

1. **Leave it (recommended).** A file from the fork before this change holds pixels, and on a
   scaled Wayland or macOS desktop its first start opens a window larger by the scale; the
   compositor limits it to the display, and the size the player then leaves is stored right.
   Decision 2 accepted this.
2. **A migration flag.** The migration marks a fork file that holds a window size and lacks a new
   marker, through a declared `[Video]` setting that `openWindow` reads once: it divides the size by
   the density of the desktop mode on `[Video] screen`'s display, and clears the flag. Cost: a
   setting that exists for one start, in the defaults, the docs and `rtxsupport.cpp`'s list.

**Still to run by hand.** The plan's check on two displays at different scales needs the internal
eDP enabled next to the G9 (`kscreen-doctor output.eDP-1.enable`), which changes the desktop and
waits for the user's approval: start windowed on each display three times, and the stored size must
not change.

## Item 4, done: the check in the real game

`components-tests` covers the rule. The check in the game is left to the user: minimize
`./omw game` through KWin and read `nvidia-smi --query-gpu=utilization.gpu --format=csv -l 1`; the
use must fall to the desktop's own, and come back on restore.
