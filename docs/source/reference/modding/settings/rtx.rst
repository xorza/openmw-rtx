RTX Settings
############

The experimental Vulkan ray tracing renderer. It replaces primary visibility, shadows, direct and
indirect light, sky, water and fog; the OpenGL renderer is untouched and is what you get with
:code:`enabled = false`.

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
   :range: ≥ 0
   :default: 4

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
   :default: native

   Put the upscaler, AMD's FSR 3.1, between the trace and the screen. The window's size is what
   comes out; what gets traced is that size over the mode's ratio: 3 for :code:`ultraperformance`,
   2 for :code:`performance`, 1.7 for :code:`balanced`, 1.5 for :code:`quality` and 1 for
   :code:`native`, which upscales nothing and reconstructs each frame from the frames before it, as
   the anti-aliasing. :code:`off` traces at the window's size with no anti-aliasing, and the menus do
   not offer it. A name this does not know is refused rather than quietly defaulted.

.. omw-setting::
   :title: specular map layout
   :type: string
   :range: ignore, metal roughness
   :default: ignore

   What the content's :code:`_spec` maps mean. The file cannot say, and two layouts are in use:
   OpenMW's own, with a highlight colour in RGB and glossiness in alpha, and the one of the PBR
   packs, with metalness in red, roughness in green, ambient occlusion in blue and one less
   subsurface scattering in alpha. :code:`ignore` reads no specular map, which is right for the
   first and for content with none. :code:`metal roughness` reads the second.

   The maps are found by name as :ref:`auto use object specular maps` finds them, and loaded with
   the models, so a change requires a restart. A name this does not know is refused rather than
   quietly defaulted.
