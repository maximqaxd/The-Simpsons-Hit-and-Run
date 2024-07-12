//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>
#ifdef RAD_VITAGL
#include <vitaGL.h>
#define GL_BGRA_EXT GL_BGRA
#define glGenVertexArraysOES glGenVertexArrays
#define glDeleteVertexArraysOES glDeleteVertexArrays
#define glBindVertexArrayOES glBindVertexArray
#else
#include <glad/glad.h>
#endif
