# Questions on the redesign plan

Decisions that `.notes/REDESIGN.md` cannot take without you. Each names the item, the options, a
recommendation, and what waits on it.

## 1. Soft blended edges: how the eye should shade a soft texel

**Blocked:** issue 1 (`REDESIGN.md` §1). The attempt is kept as a patch,
`.notes/REDESIGN_soft-edge.diff`, against `bfecc57105`.

**What was tried, and what it showed.** The plan's design peels a blended mask's soft texels as
panes for the eye and keeps the cut for every light ray. The tests for it pass (the eye composites
the texel, the sun still cuts it, the peel budget cuts the fifth mask). The pictures did not pass:

- **Every soft texel peeled** (`./omw shot --views=all` against `bfecc57105`): 50 of 64 pictures
  moved, the guild by 87% of its pixels. A peeled texel is shaded as a pane is, at a path's end
  with no bounce, and a leaf card's body is mostly texels a little under 255: the guild's planter
  went from warm lamplight to a flat green.
- **Only the texels under the cut peeled** (at or over a half stays a solid): the planter is
  right again, and the canopies' holes have soft edges. But the guild's rug fringe, which the cut
  drew as a dark band, is now a line of bright dots: those texels take one lamp draw and one
  occlusion ray, with no shadow denoiser behind them.

The coverage rule is not the problem. The problem is that a pane is shaded more crudely than a
solid, and a soft texel is a solid surface, not glass.

**Options.**

1. **Stochastic alpha for the eye's soft texels** (Enderton et al. 2010): a texel under the cut is
   met with a chance equal to its alpha, from a hash of the pixel and the frame, and is then shaded
   as the solid it is. The coverage is right on average and the shading is full quality. The cost:
   a frame that nothing accumulates shows a dithered edge, and the denoiser's history breaks where
   the surface changes from frame to frame, so the edge stays noisy where the upscaler is off.
2. **Shade a pane as a solid is shaded**: the direct light through the shadow bit and its denoiser,
   and a bounce. This helps glass too, but it is a large change to the launch and the pane filter,
   and the channels hold one surface a pixel.
3. **Keep the cut** and close issue 1 as a known limit.

**Recommendation: option 1, for texels under the cut only**, measured with `noise` at the pier, the
pond and the guild before it is kept. The upscaler is on by default, and it resolves a dithered
edge as it resolves every other edge.

## 2. Groundcover: approval for an upstream fix

**Blocked:** issue 3 (`REDESIGN.md` §3). The fork's rules let an upstream bug fix in only after you
say yes. The fix bakes each groundcover shape's own transform into its instanced geometry in
`InstancingVisitor`, so the rasterizer applies the plant's transform after the shape's, as every
other reference does.

**Options:** approve the fix; or leave upstream as it is and close the issue as upstream's.

**Recommendation: approve it.** First, a scan of the groundcover models you play counts the shapes
under a transform that is not the identity. That says how much it matters.

## 3. Host timings: a quiet desktop for the experiments

**Blocked:** issue 4 (`REDESIGN.md` §4). The experiments measure host timings, and `AGENTS.md` says
a run started from this session moves the host rows by half. They need a desktop with no session in
the foreground. A leg is one run: after one warm-up run, run each line below six times back to back,
with the session idle, and keep each report. Both the personality `setarch -R` sets and the tunables
pass from the driver to the harness it starts.

```
./omw release bench --views=one-cell-walk
setarch -R ./omw release bench --views=one-cell-walk
GLIBC_TUNABLES=glibc.malloc.arena_max=1 ./omw release bench --views=one-cell-walk
GLIBC_TUNABLES=glibc.malloc.hugetlb=1 ./omw release bench --views=one-cell-walk
GLIBC_TUNABLES=glibc.malloc.arena_max=1:glibc.malloc.hugetlb=1 ./omw release bench --views=one-cell-walk
```

**Options:** run them yourself at a quiet time; or let a session run them unattended, with nothing
else open on the desktop.

**Recommendation:** run them yourself. The design that §4 lists for the winning experiment follows
from the five reports.
