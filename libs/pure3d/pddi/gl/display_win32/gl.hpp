//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>
// this is neccesary because of windows api issues

#ifndef APIENTRY  
    // windows.h is not included, define API delcspecs needed bu gl.h
    #define APIENTRY    __stdcall
    #define WINAPI      __stdcall
    #define WINGDIAPI   __declspec(dllimport)
    #include <glad/glad.h>
    #undef WINAPI
    #undef WINGDIAPI   
    #undef APIENTRY   
#else 
    // we are included after window.h, don't bother doing any trickery
    #include <glad/glad.h>
#endif
