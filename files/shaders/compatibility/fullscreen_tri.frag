#version 120

varying vec2 uv;

#include "lib/core/fragment.h.glsl"

#if @gammaContrast
uniform float inverseGamma;
uniform float contrast;
#endif

void main()
{
    gl_FragColor = samplerLastShader(uv);
#if @gammaContrast
    gl_FragColor.rgb = pow(clamp((gl_FragColor.rgb - 0.5) * contrast + 0.5, 0.0, 1.0), vec3(inverseGamma));
#endif
}
