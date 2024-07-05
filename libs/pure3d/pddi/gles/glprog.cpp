//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glprog.hpp>

#include <vector>
#include <SDL.h>

pglProgram::pglProgram()
{
    program = 0;
    modelview = projection = sampler = -1;
}

pglProgram::~pglProgram()
{
    if (program)
        glDeleteProgram(program);
}

void pglProgram::SetProjectionMatrix(const GLfloat* matrix)
{
    if (projection < 0)
        return;
    glUniformMatrix4fv(projection, 1, GL_FALSE, matrix);
}

void pglProgram::SetModelViewMatrix(const GLfloat* matrix)
{
    if (modelview < 0)
        return;
    glUniformMatrix4fv(modelview, 1, GL_FALSE, matrix);
}

void pglProgram::SetSampler(GLint texture)
{
    if (sampler < 0)
        return;
    glUniform1i(sampler, texture);
}

bool pglProgram::LinkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    program = glCreateProgram();

    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "normal");
    glBindAttribLocation(program, 2, "texcoord");
    glBindAttribLocation(program, 3, "color");

    if(vertexShader)
        glAttachShader(program, vertexShader);
    if(fragmentShader)
        glAttachShader(program, fragmentShader);

    glLinkProgram(program);

    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, (int *)&isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

        SDL_Log("Program linking error: %s", infoLog.data());
        return false;
    }
    
    modelview = glGetUniformLocation(program, "modelview");
    projection = glGetUniformLocation(program, "projection");
    sampler = glGetUniformLocation(program, "sampler");

    // Always detach shaders after a successful link
    if(vertexShader)
        glDetachShader(program, vertexShader);
    if(fragmentShader)
        glDetachShader(program, fragmentShader);
    return true;
}
