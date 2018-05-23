# deshade

deshade is a library that allows you to dump and replace the shaders of
any OpenGL application, which includes: GL2 + extensions, GL3, GL4, EGL,
GLES2, GLES3 and GLvnd without recompiling the application for Linux.

# Building
To build just run make
```
make
```

# Running
By default, deshade will not dump an application shaders to disk to
be replaced, unless a `shaders` directory exists where the application
is invoked.

```
mkdir shaders
LD_PRELOAD=./deshade.so application
```

The shaders will be written to the `shaders` directory, their names
will be the hash of their contents, with the following extension scheme:
`_vs.glsl` for vertex shaders, `_fs.glsl` for fragment shaders, `_gs.glsl`
for geometry shaders, `_cs.glsl` for compute shaders, `_tsc.glsl` for
tesseleation control shaders and `_tse.glsl` for tesselation evaluation
shaders.

## Replacing Shaders
Modifying the contents of one of the dumpped shaders in the `shaders`
directory will take effect the next time the application is launched
with deshade.

## Debug Output
A debug log is also written to `deshade.txt` containing introspection
information, if deshade fails to work check this for more information.

# Linking
You can also link an application against deshade but it must come before
OpenGL on the linker command line. This isn't officially supported.

# How it works
There are several ways OpenGL can be loaded.

* application dlopens `libGL.so`
* application dlopens `libGLX_{vendor}.so`, where vendor can be `nvidia`, `intel`, `amd`, `mesa`, etc
* application fetches OpenGL functions with `glXGetProcAddress` or `glXGetProcAddressARB`
* application links directly with `-lGL`
  * this loads functions through GLX
  * or, this loads functions through `__glx_Main` imports if using `GLvnd`

deshade attempts to supplement all of these so that regardless of how
the application acquires an OpenGL context this should work.

# Known bugs
Applications which use multiple OpenGL contexts per thread may fail to
work due to the way deshade only maintains one set of function pointers
for the first context. Support for multiple contexts per thread would
require supplementing the `glXCreateContext`, `glXDestroyContext` and
`glXMakeCurrent` functions.

deshade exploits internal glibc dynamic linker functions to replace the
dynamic linker itself to handle any applications that get OpenGL
through `dlopen`, as a result this library is glibc specific and will
not work on BSDs or musl based distributions.

# Example
Here's an example of dumping the shaders `mpv` uses for `-vo=gpu`
```
[deshade]# LD_PRELOAD=./deshade.so mpv test.mp4
Playing: test.mp4
 (+) Video --vid=1 (*) (h264 1280x720 23.976fps)
 (+) Audio --aid=1 --alang=eng (*) (aac 2ch 48000Hz)
AO: [pulse] 48000Hz stereo 2ch float
VO: [gpu] 1280x720 yuv420p
AV: 00:04:21 / 00:10:58 (39%) A-V:  0.000


Exiting... (Quit)
[deshade]# cat deshade.txt
... several lines of dlopen, dlclose and dlsym
Intercepted: dlsym(0x7f0178209470 /* libGLX_nvidia.so.0 */, "__glx_Main" = 0x7f018c592c10 /* replaced with 0x7f01a110a9b0 */
Intercepted: "glXGetProcAddress" 0x7f01995c1500 /* replaced with 0x7f01a110a800 */
Intercepted: "glXGetProcAddressARB" 0x7f01995c1510 /* replaced with 0x7f01a110a650 */
... more lines of dlopen, dlclose and dlsym
Intercepted: "glCreateShader" 0x7f019582b460 /* replaced with 0x7f01a110b570 */
Intercepted: "glDeleteShader" 0x7f019582bc40 /* replaced with 0x7f01a110b3d0 */
Intercepted: "glShaderSource" 0x7f019583a240 /* replaced with 0x7f01a110ba00 */
... more lines of dlopen, dlclose and dlsym
Created vertex shader "2"
Dumpped vertex shader "0600301E6C0B14E9C07851EEE07C575F"
Source vertex shader "0600301E6C0B14E9C07851EEE07C575F"
Deleted vertex shader "2"
Created fragment shader "3"
Dumpped fragment shader "A893C72D6B1A1007AF56E9B6D4F18267"
Source fragment shader "A893C72D6B1A1007AF56E9B6D4F18267"
Deleted fragment shader "3"
Created vertex shader "5"
Dumpped vertex shader "1938651636DAB20B6300B7DDD78654FA"
Source vertex shader "1938651636DAB20B6300B7DDD78654FA"
Deleted vertex shader "5"
Created fragment shader "6"
Dumpped fragment shader "045D796EC4C8EF36F8E6BA573F93E8AE"
Source fragment shader "045D796EC4C8EF36F8E6BA573F93E8AE"
Deleted fragment shader "6"
Forwarding: dlclose(0x7f0178209470 /* libGLX_nvidia.so.0 */) = 0

[deshade]# cat shaders/*
#version 440
#define tex1D texture
#define tex3D texture
#define LUT_POS(x, lut_size) mix(0.5 / (lut_size), 1.0 - 0.5 / (lut_size), (x))
out vec4 out_color;
in vec2 texcoord;
in vec4 ass_color;
layout(std140, binding=0) uniform UBO {
layout(offset=0) vec3 src_luma;
layout(offset=16) vec3 dst_luma;
};
uniform sampler2D osdtex;
void main() {
vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
color = vec4(ass_color.rgb, ass_color.a * texture(osdtex, texcoord).r);
// color mapping
color.rgb *= vec3(1.000000);
color.rgb *= vec3(1.000000);
out_color = color;
}
#version 440
#define tex1D texture
#define tex3D texture
#define LUT_POS(x, lut_size) mix(0.5 / (lut_size), 1.0 - 0.5 / (lut_size), (x))
in vec2 vertex_position;
in vec2 vertex_texcoord0;
out vec2 texcoord0;
in vec2 vertex_texcoord1;
out vec2 texcoord1;
in vec2 vertex_texcoord2;
out vec2 texcoord2;
void main() {
gl_Position = vec4(vertex_position, 1.0, 1.0);
texcoord0 = vertex_texcoord0;
texcoord1 = vertex_texcoord1;
texcoord2 = vertex_texcoord2;
}
#version 440
#define tex1D texture
#define tex3D texture
#define LUT_POS(x, lut_size) mix(0.5 / (lut_size), 1.0 - 0.5 / (lut_size), (x))
in vec2 vertex_position;
in vec2 vertex_texcoord;
out vec2 texcoord;
in vec4 vertex_ass_color;
out vec4 ass_color;
void main() {
gl_Position = vec4(vertex_position, 1.0, 1.0);
texcoord = vertex_texcoord;
ass_color = vertex_ass_color;
}
#version 440
#define tex1D texture
#define tex3D texture
#define LUT_POS(x, lut_size) mix(0.5 / (lut_size), 1.0 - 0.5 / (lut_size), (x))
out vec4 out_color;
in vec2 texcoord0;
in vec2 texcoord1;
in vec2 texcoord2;
layout(std140, binding=0) uniform UBO {
layout(offset=0) mat3 colormatrix;
layout(offset=48) vec3 colormatrix_c;
layout(offset=64) vec3 src_luma;
layout(offset=80) vec3 dst_luma;
layout(offset=96) vec2 texture_size0;
layout(offset=112) mat2 texture_rot0;
layout(offset=144) vec2 texture_off0;
layout(offset=152) vec2 pixel_size0;
layout(offset=160) vec2 texture_size1;
layout(offset=176) mat2 texture_rot1;
layout(offset=208) vec2 texture_off1;
layout(offset=216) vec2 pixel_size1;
layout(offset=224) vec2 texture_size2;
layout(offset=240) mat2 texture_rot2;
layout(offset=272) vec2 texture_off2;
layout(offset=280) vec2 pixel_size2;
};
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
void main() {
vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
color.r = 1.000000 * vec4(texture(texture0, texcoord0)).r;
color.g = 1.000000 * vec4(texture(texture1, texcoord1)).r;
color.b = 1.000000 * vec4(texture(texture2, texcoord2)).r;
color = color.rgbr;
color.rgb = mat3(colormatrix) * color.rgb + colormatrix_c;
color.a = 1.0;
// color mapping
color.rgb *= vec3(1.000000);
color.rgb *= vec3(1.000000);
out_color = color;
}
```
