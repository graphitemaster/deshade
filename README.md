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
LD_PRELOAD=./deshade application
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
require supplementing the `glXCreateContext`, `glxDestroyContext` and
`glxMakeCurrent` functions.

deshade exploits internal glibc dynamic linker functions to replace the
dynamic linker itself to handle any applications that get OpenGL
through `dlopen`, as a result this library is glibc specific and will
not work on BSDs or musl based distributions.
