//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glprog.hpp>
#include <pddi/gles/glmat.hpp>

#include <string>
#include <vector>
#include <SDL.h>

static inline void UniformColour(GLint loc, pddiColour c)
{
    glUniform4f(loc, float(c.Red()) / 255, float(c.Green()) / 255, float(c.Blue()) / 255, float(c.Alpha()) / 255);
}

pglProgram::pglProgram()
{
    program = 0;
    projection = modelview = normalmatrix = sampler = -1;
}

pglProgram::~pglProgram()
{
    if (program)
        glDeleteProgram(program);
}

void pglProgram::SetProjectionMatrix(const pddiMatrix* matrix)
{
    if (projection >= 0)
        glUniformMatrix4fv(projection, 1, GL_FALSE, matrix->m[0]);
}

void pglProgram::SetModelViewMatrix(const pddiMatrix* matrix)
{
    if (modelview >= 0)
        glUniformMatrix4fv(modelview, 1, GL_FALSE, matrix->m[0]);
    if (normalmatrix >= 0)
    {
        pddiMatrix inverse;
        inverse.Invert(*matrix);
        inverse.Transpose();
        glUniformMatrix4fv(normalmatrix, 1, GL_FALSE, inverse.m[0]);
    }
}

void pglProgram::SetTextureEnvironment(const pglTextureEnv* texEnv)
{
    if (sampler >= 0)
        glUniform1i(sampler, 0);

    glUniform1i(lit, texEnv->lit ? 1 : 0);
    UniformColour(acm, texEnv->ambient);
    UniformColour(ecm, texEnv->emissive);
    UniformColour(dcm, texEnv->diffuse);
    UniformColour(scm, texEnv->specular);
    glUniform1f(srm, texEnv->shininess);
}

void pglProgram::SetLightState(int handle, const pddiLight* lightState)
{
    if( handle >= PDDI_MAX_LIGHTS )
        return;

    float dir[4];
    switch(lightState->type)
    {
        case PDDI_LIGHT_DIRECTIONAL :
            dir[0] = -lightState->worldDirection.x;
            dir[1] = -lightState->worldDirection.y;
            dir[2] = -lightState->worldDirection.z;
            dir[3] = 0.0f;
            break;

        case PDDI_LIGHT_POINT :
            dir[0] = lightState->worldPosition.x;
            dir[1] = lightState->worldPosition.y;
            dir[2] = lightState->worldPosition.z;
            dir[3] = 1.0f;
            break;

        case PDDI_LIGHT_SPOT :
            PDDIASSERT(0);
            break;
    }

    glUniform1i(lights[handle].enabled, lightState->enabled ? 1 : 0);
    glUniform4fv(lights[handle].position, 1, dir);
    UniformColour(lights[handle].colour, lightState->colour);
}

void pglProgram::SetAmbientLight(pddiColour ambient)
{
    UniformColour(acs, ambient);
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
    
    projection = glGetUniformLocation(program, "projection");
    modelview = glGetUniformLocation(program, "modelview");
    normalmatrix = glGetUniformLocation(program, "normalmatrix");
    sampler = glGetUniformLocation(program, "sampler");

    for (int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        std::string prefix = std::string("lights[") + char('0' + i) + "].";
        lights[i].enabled = glGetUniformLocation(program, (prefix + "enabled").c_str());
        lights[i].position = glGetUniformLocation(program, (prefix + "position").c_str());
        lights[i].colour = glGetUniformLocation(program, (prefix + "colour").c_str());
    }

    lit = glGetUniformLocation(program, "lit");
    acs = glGetUniformLocation(program, "acs");
    acm = glGetUniformLocation(program, "acm");
    dcm = glGetUniformLocation(program, "dcm");
    scm = glGetUniformLocation(program, "scm");
    ecm = glGetUniformLocation(program, "ecm");
    srm = glGetUniformLocation(program, "srm");

    // Always detach shaders after a successful link
    if(vertexShader)
        glDetachShader(program, vertexShader);
    if(fragmentShader)
        glDetachShader(program, fragmentShader);
    return true;
}
