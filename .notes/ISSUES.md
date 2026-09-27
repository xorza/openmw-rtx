# Open issues

- `rtx-gpu-tests`: `RtxDeviceTest.theDeviceAndTheRendererAreMadeWithoutAValidationError` takes 3.4 s, over the 1 s a single test may take.
- A film take sets the game time scale to nought (`Stager::stage`), and the sky's clock steps by that scale (`Sky::skyStep`), so the fog does not drift and the cloud deck does not scroll through a take, where the played game moves both.
- `view --pos=-10710.913,-75173.39,368.08698` at `-2,-10` on the M[FR] profile prints a `pos` of z 206.04921 when the window closes after 40 frames: the eye stood 162 units lower than asked.
- A `view` window's Home block at `-2,-10` printed `--pos=-9490.585,-74260.58,449.53494 --look=-8888.172,-73467.5,539.6521` (bearing 37°), while the frame on screen faced about 265° from that position: a shot at that pos facing 265° matches the window's picture, and one at the printed look shows the ship at the docks.
- `shadeWater` marks a reflection found where `bounced.mDistance < WATER_MAX_PATH`, and `WATER_MAX_PATH` is 2000 units, so a reflection of anything further than 2000 units is written as not found: `Channel::ReflectionMotion` holds no mirrored vector for it.
