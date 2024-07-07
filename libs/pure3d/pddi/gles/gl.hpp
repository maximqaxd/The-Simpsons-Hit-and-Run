//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>
#if defined(RAD_VITA) && !defined(RAD_USE_PVR)
#include <vitaGL.h>
#define GL_BGRA_EXT GL_BGRA
#else
#include <glad/glad.h>
#endif
