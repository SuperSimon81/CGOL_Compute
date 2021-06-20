$input v_color0,v_texcoord0

#include <bgfx_shader.sh>
SAMPLER2D(s_tex,  0);


void main()
{
    //gl_FragColor = vec4(1,1,1,1); // v_color0;
    //float col = texture2D(s_tex,v_texcoord0);
    gl_FragColor = texture2D(s_tex,v_texcoord0);
}