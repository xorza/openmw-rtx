RTX Settings
############

The Vulkan ray tracing renderer, the one renderer the game starts. It draws primary visibility,
shadows, direct and indirect light, sky, water and fog. Upstream's OpenGL renderer stays in the build
and nothing starts it, so the settings only it reads have no effect.

The ray tracer needs a GPU with hardware ray tracing, NVIDIA Turing or AMD RDNA 2 or later:
acceleration structures, ray query, ray tracing pipelines and position fetch are all required. On a
device missing any of them the game does not start, and its error box says what is missing.

Most settings here are read once, at startup. :code:`upscale`, :code:`indirect lighting`,
:code:`bounce rays` and :code:`distant land cells` also follow the settings window while the game
runs.

.. omw-setting::
   :title: distant land cells
   :type: float32
   :range: 0 to 10
   :default: 10

   How far out from the eye the world is built, in cells. Rays go everywhere, so this says how much
   world exists rather than how far the camera can see, and the fog closes at the same distance —
   air tuned to a shorter reach makes a world built four cells out look like one built none.

   Zero hands the decision back to :code:`viewing distance` in the camera section, which answers a
   different question for a renderer that culls: at 7168 units against a cell of 8192 it barely
   leaves the active grid.

   Takes effect at once: the ring, the air and the map follow the new reach. :code:`object paging`
   and :code:`object paging min size` from the terrain section decide whether and how the distance's
   statics stand, and those two are read once at start.

.. omw-setting::
   :title: upscale
   :type: string
   :range: off, ultraperformance, performance, balanced, quality, native
   :default: balanced

   Put the selected upscaler between the trace and the screen. The frame's size,
   :code:`[Video] resolution`, is what comes out; what gets traced is that size over the mode's
   ratio: 3 for :code:`ultraperformance`, 2 for :code:`performance`, 1.7 for :code:`balanced`, 1.5 for :code:`quality` and 1 for
   :code:`native`, which upscales nothing and reconstructs each frame from the frames before it, as
   the anti-aliasing. :code:`off` traces at the frame's size with no upscaler: the denoisers alone,
   and no anti-aliasing. A name this does not know is refused rather than quietly defaulted.

.. omw-setting::
   :title: upscaler
   :type: string
   :range: auto, fsr, dlss
   :default: auto

   Which upscaler does the work above. :code:`auto` uses NVIDIA DLSS where the card is an NVIDIA
   RTX, the installed driver accepts it, and the DLSS feature library is installed beside the
   game; FSR 3.1 everywhere else. :code:`fsr` never looks for DLSS, which is what an AMD card or an
   A/B against FSR wants. :code:`dlss` refuses to start where DLSS cannot, naming the reason, rather
   than quietly drawing through FSR. A build made with :code:`OPENMW_RTX_DLSS=OFF` has only
   :code:`fsr`.

   The game links none of NVIDIA's SDK. The driver supplies NGX — :code:`_nvngx.dll` on Windows
   and :code:`libnvidia-ngx.so.1` on Linux — and the game opens it at run time. The feature
   library, :code:`nvngx_dlss.dll` on Windows or :code:`libnvidia-ngx-dlss.so.*` on Linux, is not
   part of OpenMW. The launcher's **Download DLSS** button shows `NVIDIA's licence
   <https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt>`__ and fetches the pinned 310.9.16
   library from NVIDIA's release after accepting it, into OpenMW's user data folder: on Windows
   :code:`Documents/My Games/openmw/dlss`, on Linux :code:`$XDG_DATA_HOME/openmw/dlss` (usually
   :code:`~/.local/share/openmw/dlss`). The launcher disables the DLSS choice until the library is
   installed. The game also looks beside its own executable, where a manually downloaded copy can
   be placed; NGX itself looks there first. DLSS does not appear on a machine without an NVIDIA
   display adapter.

   DLSS keeps its default preset, so NVIDIA's over-the-air model updates reach a build that
   predates them: quality, balanced and native use the transformer preset K, performance preset M,
   and ultra performance preset L, which is DLSS 4.5 Super Resolution. Read once, where the
   renderer is made: the launcher and the settings window offer all three, and the settings window
   says a change needs a restart.

.. omw-setting::
   :title: indirect lighting
   :type: boolean
   :default: true

   Whether to trace indirect light: light that reaches a surface off another surface, path traced.
   Off, flat ambient light stands in for it, which saves the most on a slow GPU, and a glossy surface
   reflects only its lamps. :code:`bounce rays` says nothing then.

   Takes effect at once.

.. omw-setting::
   :title: bounce rays
   :type: int
   :range: 1 to 4
   :default: 1

   How many bounce rays the surface each pixel shows traces a frame. Each takes a direction of its own
   and lights where it lands, and the rays are averaged: the bounced light's noise falls as one over
   the square root of the count, which leaves the denoiser and the upscaler less to hide when the
   camera moves. Each ray costs about what the first one does.

   Takes effect at once.

What the ray tracer declines
****************************

Only the OpenGL renderer reads these, and the game never starts it: the settings window and the
launcher do not offer them, and a value in :code:`settings.cfg` changes nothing. The console and Lua
answer the requests below with "not available under this renderer" and the reason.

Settings
========

* ``[Camera] near clip``: The trace starts each ray at the eye, and no near plane cuts the picture.
* ``[Camera] small feature culling``: The trace culls nothing by its size on the screen.
* ``[Camera] small feature culling pixel size``: The trace culls nothing by its size on the screen.
* ``[Cells] cache expiry delay``: This sets how long the rasterizer keeps its chunks. The ray tracer keeps what its ring of cells holds.
* ``[Cells] target framerate``: This sets the rasterizer's loading budget for each frame. The ray tracer loads cells off the frame.
* ``[Fog]`` every key: The ray tracer integrates the air along each ray, as dense as the weather's own fog depth makes it.
* ``[General] texture mag filter``: The ray tracer filters every texture trilinearly.
* ``[General] texture min filter``: The ray tracer filters every texture trilinearly.
* ``[General] texture mipmap``: The ray tracer filters every texture trilinearly.
* ``[Groundcover] stomp intensity``: The ray tracer stands each plant still: no wind and no step bends it.
* ``[Groundcover] stomp mode``: The ray tracer stands each plant still: no wind and no step bends it.
* ``[Post Processing]`` every key: Shader post-processing runs on the rasterizer. The ray tracer has its own exposure, bloom and tone curve.
* ``[Shaders] adjust coverage for alpha test``: The trace cuts an alpha-tested surface for each ray, with no coverage to adjust.
* ``[Shaders] antialias alpha test``: The trace cuts an alpha-tested surface for each ray, with no coverage to adjust.
* ``[Shaders] auto use object normal maps``: The ray tracer reads every model's normal maps and specular maps, each specular map in the layout its channels show.
* ``[Shaders] auto use object specular maps``: The ray tracer reads every model's normal maps and specular maps, each specular map in the layout its channels show.
* ``[Shaders] clamp lighting``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] classic falloff``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] clustered lighting``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] force per pixel lighting``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] light fade start``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] light radius multiplier``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] match sunlight to sun``: The ray tracer's sun stands where the sun's disc is.
* ``[Shaders] max lights``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] maximum light distance``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] minimum interior brightness``: The ray tracer lights a room by the ambient its record states.
* ``[Shaders] particle point lighting``: The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.
* ``[Shaders] soft particles``: The trace meets a particle as a volume, which a wall cuts softly.
* ``[Shaders] weather particle occlusion small feature culling pixel size``: The trace culls nothing by its size on the screen.
* ``[Shadows]`` every key: Each surface casts a traced shadow.
* ``[Stereo]`` every key: The ray tracer draws one eye.
* ``[Stereo View]`` every key: The ray tracer draws one eye.
* ``[Terrain] composite map level``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] composite map resolution``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] debug chunks``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] distant terrain``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] lod factor``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] max composite geometry size``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] object paging active grid``: This sets how the rasterizer merges distant objects. The ray tracer places each distant object whole.
* ``[Terrain] object paging merge factor``: This sets how the rasterizer merges distant objects. The ray tracer places each distant object whole.
* ``[Terrain] object paging min size cost multiplier``: This sets how the rasterizer merges distant objects. The ray tracer places each distant object whole.
* ``[Terrain] object paging min size merge factor``: This sets how the rasterizer merges distant objects. The ray tracer places each distant object whole.
* ``[Terrain] vertex lod mod``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Terrain] water culling``: This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.
* ``[Video] antialiasing``: The ray tracer's reconstruction resolves the edges.
* ``[Water]`` every key: The ray tracer reflects and refracts each water surface by its own rays.

Render modes and requests
=========================

* Wireframe (``tww``, ``debug.toggleRenderMode``): The ray tracer draws no wireframe.
* Shader reload (``debug.triggerShaderReload``): The ray tracer's kernels are compiled into the build, and a rebuild changes them.
* Live shader reload (``debug.setShaderHotReloadEnabled``): The ray tracer's kernels are compiled into the build, and a rebuild changes them.
