#version 120
#extension GL_EXT_texture_array : require

varying vec2 uv;
uniform sampler2DArray lastShader;

#if @gammaContrast
uniform float inverseGamma;
uniform float contrast;
#endif

void main()
{
    int view = 0;
    vec3 uvz = vec3(uv.x * 2., uv.y, 0);
    if(uvz.x > 1.)
    {
        uvz.x -= 1.;
        uvz.z = 1;
    }

    gl_FragColor = texture2DArray(lastShader, uvz);
#if @gammaContrast
    gl_FragColor.rgb = pow(clamp((gl_FragColor.rgb - 0.5) * contrast + 0.5, 0.0, 1.0), vec3(inverseGamma));
#endif
}
