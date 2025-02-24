//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>

#ifdef RAD_DREAMCAST
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glkos.h>
#include <GL/glu.h>
#define GL_FUNC_ADD NULL
#define GL_FUNC_REVERSE_SUBTRACT NULL
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_BGRA_EXT GL_BGRA
#define GL_LIGHT_MODEL_COLOR_CONTROL_EXT GL_LIGHT_MODEL_COLOR_CONTROL
#define GL_SEPARATE_SPECULAR_COLOR_EXT GL_SEPARATE_SPECULAR_COLOR
#endif
#ifdef RAD_GLES
#undef glOrtho
#undef glFrustum
#undef glClearDepth
#undef glDepthRange
#undef glFogi
#define glOrtho glOrthof
#define glFrustum glFrustumf
#define glClearDepth glClearDepthf
#define glDepthRange glDepthRangef
#define glFogi glFogx
#endif
