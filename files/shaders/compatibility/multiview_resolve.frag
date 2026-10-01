#version 120
#extension GL_EXT_texture_array : require

varying vec2 uv;
uniform sampler2DArray lastShader;

#if @gamma
uniform float inverseGamma;
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
#if @gamma
    gl_FragColor.rgb = pow(max(gl_FragColor.rgb, vec3(0.0)), vec3(inverseGamma));
#endif
}
