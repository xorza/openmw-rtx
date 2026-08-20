RTX Settings
############

The experimental Vulkan ray tracing renderer. It replaces primary visibility, shadows, direct and
indirect light, sky, water and fog; the OpenGL renderer is untouched and is what you get with
:code:`enabled = false`.

It exists only in a build configured with :code:`-DOPENMW_RTX=ON`, and it needs an Ada-class NVIDIA
GPU: acceleration structures, ray query, ray tracing pipelines, position fetch, opacity micromaps and
shader execution reordering are all required, and a device missing any of them refuses to start
rather than falling back.

Both settings are read once, at startup.

.. omw-setting::
   :title: enabled
   :type: boolean
   :range: true, false
   :default: false

   Use the ray tracing renderer instead of the OpenGL one. Takes effect on the next start.

.. omw-setting::
   :title: validation
   :type: boolean
   :range: true, false
   :default: false

   Load the Vulkan validation layers. A developer setting: with the layers on, any error they report
   stops the process at the call that caused it, so the renderer cannot limp on producing undefined
   contents, and the frame rate is not representative of anything. Off costs nothing.
