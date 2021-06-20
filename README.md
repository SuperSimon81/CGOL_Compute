# cgol_compute
 Conways Game Of Life using Compute shaders. Written in C++ and GLSL using the BGFX API. It runs entirely on the GPU using compute shaders. Pingponging between two texures A and B. On even frames the compute shader reads from A and writes to B, displaying B. On odd frames vice versa. Tested on OSX but should compile on linux/windows with small modifications. Code is largely uncommented, no support will be provided, use as is.

![ScreenShot](Screenshot.png)
