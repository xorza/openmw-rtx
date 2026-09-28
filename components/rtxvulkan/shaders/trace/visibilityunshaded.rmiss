#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

// Nothing, for a ray of the arms' eye that found no arm: `MISS_RECORD_UNSHADED` says why the sky is
// not this eye's to shade. The launch reads that the ray missed and traces the world's own ray.

#include "lib/payload.glsl"

layout(location = RTX_PAYLOAD) rayPayloadInEXT VisibilityPayload packed;

void main()
{
    Answer answer = noAnswer();
    answer.mDistance = gl_RayTmaxEXT;
    packed = packAnswer(answer);
}
