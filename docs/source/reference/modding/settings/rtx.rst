RTX Settings
############

The experimental Vulkan ray tracing renderer. It replaces primary visibility, shadows, direct and
indirect light, sky, water and fog; the OpenGL renderer is what you get with :code:`enabled = false`,
and is upstream's but for the changes this fork's :code:`AGENTS.md` names: four to its picture, the
frame shown scaled into the window, and the gamma it applies in its last draw.

A build configured with :code:`-DOPENMW_RTX=OFF` leaves it out. It needs a GPU with hardware ray
tracing, NVIDIA Turing or AMD RDNA 2 or later: acceleration structures, ray query, ray tracing
pipelines, position fetch and the fused multiply-add of :code:`VK_KHR_shader_fma` are all required,
and a device missing any of them refuses to start rather than falling back.

Most settings here are read once, at startup. :code:`upscale` and :code:`distant land cells`
also follow the settings window while the game runs.

.. omw-setting::
   :title: enabled
   :type: boolean
   :range: true, false
   :default: false

   Use the ray tracing renderer instead of the OpenGL one. Takes effect on the next start.

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

   Put the upscaler, AMD's FSR 3.1, between the trace and the screen. The frame's size,
   :code:`[Video] resolution`, is what comes out; what gets traced is that size over the mode's ratio: 3 for :code:`ultraperformance`,
   2 for :code:`performance`, 1.7 for :code:`balanced`, 1.5 for :code:`quality` and 1 for
   :code:`native`, which upscales nothing and reconstructs each frame from the frames before it, as
   the anti-aliasing. :code:`off` traces at the frame's size with no upscaler: the denoisers alone,
   and no anti-aliasing. A name this does not know is refused rather than quietly defaulted.

.. omw-setting::
   :title: specular map layout
   :type: string
   :range: ignore, classic, metal roughness
   :default: ignore

   What the content's :code:`_spec` maps mean. The file cannot say, and two layouts are in use:
   OpenMW's own, with a highlight colour in RGB and glossiness in alpha, and the one of the PBR
   packs, with metalness in red, roughness in green, ambient occlusion in blue and one less
   subsurface scattering in alpha. :code:`ignore` reads no specular map, which is right for content
   with none. :code:`classic` reads the first as a reflectance and a roughness: the highlight colour
   is the reflectance at normal incidence, and the glossiness — a Blinn-Phong exponent over 255 — is
   matched to a roughness by :math:`\alpha = \sqrt{2 / (n + 2)}`, which is an approximation and not
   the rasterizer's highlight; a terrain :code:`_diffusespec` is read the same way, its alpha the
   grey reflectance at the exponent of 128 the rasterizer's terrain uses. :code:`metal roughness`
   reads the second.

   The maps are found by name as :ref:`auto use object specular maps` finds them, and loaded with
   the models, so a change requires a restart. A name this does not know is refused rather than
   quietly defaulted.

What the ray tracer declines
****************************

The settings window greys these out under the ray tracer and shows the reason as the tooltip, and
the console and Lua answer the requests below with "not available under this renderer" and the
reason. The list is the renderer's own declaration, and a test holds this page to it.

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
* ``[Groundcover]`` every key: The ray tracer does not draw groundcover yet.
* ``[Physics] async num threads``: This sets the rasterizer's draw threads.
* ``[Post Processing]`` every key: Shader post-processing runs on the rasterizer. The ray tracer has its own exposure, bloom and tone curve.
* ``[Shaders] adjust coverage for alpha test``: The trace cuts an alpha-tested surface for each ray, with no coverage to adjust.
* ``[Shaders] antialias alpha test``: The trace cuts an alpha-tested surface for each ray, with no coverage to adjust.
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
