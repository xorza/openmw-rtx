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
