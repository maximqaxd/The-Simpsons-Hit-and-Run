//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _GLPROG_HPP_
#define _GLPROG_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/basecontext.hpp>
#include <pddi/gles/gl.hpp>
struct pglTextureEnv;

class pglProgram : public pddiObject
{
public:
    pglProgram();
    ~pglProgram();

    GLuint GetProgram() { return program; }
    bool LinkProgram(GLuint vertexShader, GLuint fragmentShader);
    static bool CompileShader(GLuint shader, const char* source);
    static pglProgram* CreateProgram(GLuint vertexShader, GLuint fragmentShader);

    void SetProjectionMatrix(const pddiMatrix* matrix);
    void SetModelViewMatrix(const pddiMatrix* matrix);
    void SetTextureEnvironment(const pglTextureEnv* texEnv);
    void SetLightState(int handle, const pddiLight* lightState);
    void SetAmbientLight(pddiColour ambient);

protected:
    GLuint program;

    // Uniform locations
    GLint projection, modelview, normalmatrix, alpharef, sampler;
    struct {
        GLint enabled, position, colour, attenuation;
    } lights[PDDI_MAX_LIGHTS];
    GLint acs, acm, dcm, scm, ecm, srm;
};

#endif
