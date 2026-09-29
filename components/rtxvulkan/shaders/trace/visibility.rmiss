#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

// The sky, for a ray that reached nothing.
//
// **The whole of the sky's shading, out of the trace's own kernel.** It is fourteen percent of the
// pixels at the ship at Seyda Neen and it shares nothing with a surface: no instance row, no
// material, no lamp reservoir and no bounce. Here it is its own program with its own registers, and
// the launch that invoked it holds none of what it used.
//
// **What the backdrop shows through is this shader's to say.** `mBackdropShown` is how much of the
// star field the sky's own layers left, and the display pass cannot work it out for itself —
// `backdrop` in `bindings.glsl` says why.

#include "lib/bindings.glsl"
#include "lib/counts.glsl"
#include "lib/frame.glsl"
#include "lib/payload.glsl"
#include "lib/reproject.glsl"
#include "lib/sky.glsl"
#include "lib/variants.glsl"

layout(location = RTX_PAYLOAD) rayPayloadInEXT VisibilityPayload packed;

void main()
{
    // Whatever the answer below: a ray that found water from under it, or a picture's background,
    // reached nothing all the same.
    countMiss();

    Answer answer = noAnswer();

    const vec3 origin = gl_WorldRayOriginEXT;
    const vec3 direction = gl_WorldRayDirectionEXT;

    // **The sky moves too, and only its turn moves it** — `skyMotionOf` says why storing nothing
    // here is a smear across every camera rotation. Whatever the answer below: the water a ray
    // finds under the surface and a picture's background turn with the eye as the sky does.
    answer.mMotion = vec3(skyMotionOf(gl_LaunchIDEXT.xy, direction), 0.0);

    // **A ray that goes down from under the surface and finds nothing found water, and water is not
    // the sky.** `waterUnbounded` is the whole argument, and the launch asks it again for the column
    // the pixel is then seen through. A picture's background is nothing as well.
    const bool picture = frame.mTransparentBackground != 0u;
    if (!waterUnbounded(false, origin, direction) && !picture)
        answer.mRadiance = skyRadiance(origin, direction, pixelBlur(frame.mCamera), true, answer.mBackdropShown);

    // **And the interface behind a picture is its backdrop, shown whole**: no sky stands in front
    // of it, so whatever the launch put in front of this miss is all that covers it.
    answer.mBackdropShown = picture ? 1.0 : answer.mBackdropShown;

    answer.mDistance = gl_RayTmaxEXT;
    packed = packAnswer(answer);
}
