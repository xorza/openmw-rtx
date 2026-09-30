OpenMW RTX
==========

A fork of [OpenMW](https://openmw.org) that makes Morrowind work your GPU hard again:
ray-traced lighting and path-traced indirect light.

This tree is OpenMW 0.52 plus one renderer. Everything about the engine itself — what it is,
how to install it, how to build it, the data path, the command line — is in the
[upstream README](https://gitlab.com/OpenMW/openmw/-/blob/master/README.md). This file covers
only what the fork adds.

Screenshots
-----------

<img width="2560" height="1366" alt="screenshot017" src="https://github.com/user-attachments/assets/d99644a6-2395-4a9b-a095-53a54c7a27b2" />
<img width="2560" height="1366" alt="screenshot016" src="https://github.com/user-attachments/assets/cc181eb8-713c-41f2-a093-2aea60d71c2a" />
<img width="2560" height="1366" alt="screenshot015" src="https://github.com/user-attachments/assets/3490ca2f-df43-4850-b619-d63b28720f82" />
<img width="2560" height="1366" alt="screenshot014" src="https://github.com/user-attachments/assets/7d53a672-1eb9-404c-9ad2-107ab6a102b8" />
<img width="2560" height="1366" alt="screenshot008" src="https://github.com/user-attachments/assets/ad180077-1a39-4921-ba38-fc34aa566e55" />
<img width="2560" height="1366" alt="screenshot003" src="https://github.com/user-attachments/assets/7ecbd7a4-37f5-4761-8596-79a2870a3a75" />
<img width="2560" height="1366" alt="screenshot001" src="https://github.com/user-attachments/assets/2a5855df-bc04-4b08-929a-51edee2c4fcc" />
<img width="2560" height="1366" alt="screenshot000" src="https://github.com/user-attachments/assets/d85dd6c8-8a77-4ddc-ae83-6ac1f58d42fb" />


Demo video
----------

[Watch the demo on YouTube](https://www.youtube.com/watch?v=h9wsxzmaoqM)

[![OpenMW RTX demo](https://img.youtube.com/vi/h9wsxzmaoqM/maxresdefault.jpg)](https://www.youtube.com/watch?v=h9wsxzmaoqM)

What the fork is
----------------

Upstream OpenMW stays the host engine: cells, references, physics, scripts, animation, weather
and the GUI. It no longer owns the picture. A second renderer stands beside the OpenGL rasterizer
and replaces the whole image: primary visibility, shadows, direct and indirect light, sky, water
and fog are ray traced on the GPU. The rasterizer draws upstream's picture but for three
corrections upstream needed as well: the optimizer merges in child order, an exterior map tile
keeps its land before the quad tree builds its chunk, and a day-night switch shows its mode's child
from the first frame. One binary ships both renderers, and the one not chosen never starts.

Vanilla content is read as it is. Morrowind's textures are pre-lit, so the renderer estimates
the painted light and divides it out to recover materials the new light transport can use.

Goal
----

A 2002 game made to look astonishing on current hardware. Vanilla content, new light transport.

Requirements
------------

* An NVIDIA RTX card, Turing (RTX 20 series) or later, which is what the renderer is written and
  tested for. An AMD card, RDNA 2 (RX 6000 series) or later, starts too, but nothing has run on
  one yet.
* Vulkan 1.4 with ray tracing pipelines, ray queries, position fetch and `VK_KHR_shader_fma`. A
  device missing any of them refuses to start rather than falling back.
* A driver that offers `VK_KHR_shader_fma`: NVIDIA 595 or later, AMD 26.3.1 or later, or Mesa 26.2
  or later on Linux. An older driver is refused for that extension and runs once it is updated.

Tested on one machine so far: a laptop RTX 4090 at 150 W, which is about a desktop RTX 4070.

Building and running
--------------------

The renderer is built by default. `-DOPENMW_RTX=OFF` leaves it out. Turn the renderer on with
`[RTX] enabled = true` in `settings.cfg`.

* [Architecture](docs/rtx/architecture.md) — the seam, the layers, who owns whom, the order a
  frame is computed in
* [Settings](docs/source/reference/modding/settings/rtx.rst) — every `[RTX]` setting

When it crashes
---------------

The game writes a report of every crash, and of every hang longer than 20 seconds. Once the game
is gone, the log and every dump of the session go into one file, `OpenMW-crash-<time>.zip` under
`crashes` in the user data folder, and a dialog names it. Its **Report the crash** button opens a
[new issue](https://github.com/xorza/openmw-rtx/issues) filled in with the crash, and the folder,
so the file can be dragged into the issue. Inside it:

* `openmw.log`. Its last lines, which begin `Crash:` or `Hang:`, say what happened and what the
  game was doing.
* The dump: every thread's stack.

Each release publishes its symbols, `-symbols.zip`, beside its archive, and
`./omw crash <dump> <symbols>` reads a dump against them. `[General] crash hang seconds`
sets the hang limit, and `OPENMW_DISABLE_CRASH_CATCHER=1` turns the catcher off.

License
-------

GPLv3, as upstream. See [LICENSE](LICENSE).
