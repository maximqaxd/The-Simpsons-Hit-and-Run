//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _GLPROG_HPP_
#define _GLPROG_HPP_

#include <pddi/pddi.hpp>
#include <pddi/gles/gl.hpp>

class pglProgram : public pddiObject
{
public:
    pglProgram();
    ~pglProgram();

    GLuint GetProgram() { return program; }
    bool LinkProgram(GLuint vertexShader, GLuint fragmentShader);

    void SetProjectionMatrix(const GLfloat* matrix);
    void SetModelViewMatrix(const GLfloat* matrix);
    void SetSampler(GLint texture);

protected:
    GLuint program;
    GLint modelview, projection, sampler;
};

#endif
