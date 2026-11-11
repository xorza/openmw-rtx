OpenMW RTX
==========

A fork of [OpenMW](https://openmw.org) that makes Morrowind work your GPU hard again:
ray-traced lighting and path-traced indirect light.

This tree is OpenMW 0.53 with a ray tracer in place of its renderer. What the engine itself is, the
data path and the command line are in the
[upstream README](https://gitlab.com/OpenMW/openmw/-/blob/master/README.md). This file covers
only what the fork adds, and how to get it and build it, which differ from upstream.

Screenshots
-----------

<img width="2560" height="1366" alt="screenshot017" src="https://github.com/user-attachments/assets/d99644a6-2395-4a9b-a095-53a54c7a27b2" />
<img width="2560" height="1366" alt="screenshot016" src="https://github.com/user-attachments/assets/cc181eb8-713c-41f2-a093-2aea60d71c2a" />
<img width="2560" height="1366" alt="screenshot015" src="https://github.com/user-attachments/assets/3490ca2f-df43-4850-b619-d63b28720f82" />
<img width="2560" height="1366" alt="screenshot014" src="https://github.com/user-attachments/assets/7d53a672-1eb9-404c-9ad2-107ab6a102b8" />
<img width="2560" height="1366" alt="screenshot008" src="https://github.com/user-attachments/assets/ad180077-1a39-4921-ba38-fc34aa566e55" />
<img width="2560" height="1366" alt="screenshot003" src="https://github.com/user-attachments/assets/7ecbd7a4-37f5-4761-8596-79a2870a3a75" />
<img width="2560" height="1366" alt="screenshot001" src="https://github.com/user-attachments/assets/2a5855df-bc04-4b08-929a-51edee2c4fcc" />


Demo video
----------

[Watch the demo on YouTube](https://youtu.be/2_a1KQA44-I)


What the fork is
----------------

Upstream OpenMW stays the host engine: cells, references, physics, scripts, animation, weather
and the GUI. It no longer owns the picture. The ray tracer draws the whole image: primary
visibility, shadows, direct and indirect light, sky, water and fog are ray traced on the GPU.

The ray tracer is the one renderer the game starts. No setting, launcher page or fallback reaches
upstream's OpenGL renderer, which stays in the build only so that upstream's code merges as it
stands.

Vanilla content and the PBR replacers made for OpenMW are read as they are. Morrowind's textures
are pre-lit, so the renderer estimates the painted light and divides it out to recover materials
the new light transport can use. What a replacer's companion maps add reaches the trace.

Changes to upstream
-------------------

* SDL3 in place of SDL2, for display scaling on high-density and Wayland desktops.
* `[Video] resolution` sets the frame size, and the frame is scaled into the window.
* The settings window and the launcher offer only what the ray tracer honours, with `[RTX]` pages
  in place of the rasterizer's.
* The interface scale and the font size apply while the game runs.
* A Light bolt that strikes no actor lights the surface it struck.
* A crash reporter of its own, built on Crashpad.
* Three fixes to upstream's picture that the ray tracer needed: the optimizer's merge order, the
  land of a map tile, and a groundcover plant's transforms.

[AGENTS.md](AGENTS.md#accepted-diff) lists every change and the reason for it.

Goal
----

A 2002 game made to look astonishing on current hardware: the content as it stands, new light
transport.

Requirements
------------

* An NVIDIA RTX card, Turing (RTX 20 series) or later, which is what the renderer is written and
  tested for. An AMD card, RDNA 2 (RX 6000 series) or later, starts too and draws right on a
  low-end AMD integrated GPU, but far too slowly there to play yet.
* Vulkan 1.4 with ray tracing pipelines, ray queries and position fetch. On a machine without them
  the game does not start, and its error box says what is missing and where the log is. No other
  renderer takes its place.
* An NVIDIA card with DLSS support may use DLSS Super Resolution instead of FSR. No NVIDIA code is
  built into or shipped with the game: the driver's NGX core is loaded at run time, and the DLSS
  feature library is supplied by the player. The launcher's **Download DLSS** button links to
  NVIDIA's licence beside it, fetches the library from NVIDIA's release and checks its SHA-256; a
  copy placed beside the executable also works. `[RTX] upscaler = auto | fsr | dlss` chooses, and
  `auto` falls back to FSR when that library is absent.

Measured so far on a high-end NVIDIA laptop GPU, an AMD handheld under RADV, and a low-end AMD
integrated GPU under AMD's Windows driver. On a slow GPU, `[RTX] indirect lighting` off saves the
most.

Getting it
----------

Each [release](https://github.com/xorza/openmw-rtx/releases) holds an AppImage for Linux and a
portable folder, zipped, for Windows, with the launcher and the installation wizard inside.

Building and running
--------------------

`./omw` is the one way to build, on Linux and on Windows (`omw.cmd`), and needs Python 3.11 or later.
Every library comes from the dependency set the driver fetches, built static by vcpkg (`vcpkg.json`); the
system keeps what the programs open at run time, whose development files `sudo CI/install_ubuntu_deps.sh`
installs on Ubuntu 26.04 and `CI/install_almalinux_deps.sh` on AlmaLinux 8. On Windows MSVC is all it needs.
CMake and clang-format are the driver's own pinned ones on both.

```
./omw bootstrap                          # the pinned dependency set and CMake, into deps/
./omw release build
./omw release setup <Morrowind folder>   # the openmw.cfg a fresh machine has none of
./omw release exec ./openmw
```

`./omw help` lists the rest: the flavours, the tests, and the harness that checks and measures the
renderer without a window.

* [Architecture](docs/rtx/architecture.md) — the seam, the layers, who owns whom, the order a
  frame is computed in
* [Settings](docs/source/reference/modding/settings/rtx.rst) — every `[RTX]` setting, and what the
  ray tracer declines of upstream's

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
`./omw crash <dump> <symbols>` reads a dump, or the package, against them. `[General] crash hang seconds`
sets the hang limit, and `OPENMW_DISABLE_CRASH_CATCHER=1` turns the catcher off.

License
-------

GPLv3, as upstream. See [LICENSE](LICENSE).
