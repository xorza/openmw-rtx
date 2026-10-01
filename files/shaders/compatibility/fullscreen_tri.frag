#version 120

varying vec2 uv;

#include "lib/core/fragment.h.glsl"

#if @gamma
uniform float inverseGamma;
#endif

void main()
{
    gl_FragColor = samplerLastShader(uv);
#if @gamma
    gl_FragColor.rgb = pow(max(gl_FragColor.rgb, vec3(0.0)), vec3(inverseGamma));
#endif
}
