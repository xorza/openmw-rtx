# Open issues

- Strafed (`noise --strafe=150`) at `quality`, the pond and the guild are noisier than 16 frames
  averaged (2.64/21 against 2.54/19, 4.96/39 against 4.54/38); still, and strafed at native, both
  pass. The rise is at edges: the canopy's leaf cutouts, and the outlines of the sky's reflection on
  the water, whose motion vectors are the surface's and not the reflected image's.
- `noise --upscale=off` holds an unjittered frame against a jittered reference and bar, so it counts
  the frame's aliasing as noise: the still guild fails at 4.77/76, with a mean error of 16.9 on edges
  against 3.6 on surfaces.
