//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glmat.hpp>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glcon.hpp>

#include <vector>
#include <microprofile.h>
#include <SDL.h>

pddiShadeColourTable pglMat::colourTable[] = 
{
    {PDDI_SP_AMBIENT  , SHADE_COLOUR(&pglMat::SetAmbient)},
    {PDDI_SP_DIFFUSE  , SHADE_COLOUR(&pglMat::SetDiffuse)},
    {PDDI_SP_EMISSIVE , SHADE_COLOUR(&pglMat::SetEmissive)},
    {PDDI_SP_SPECULAR , SHADE_COLOUR(&pglMat::SetSpecular)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pglMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pglMat::SetTexture)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeIntTable pglMat::intTable[] = 
{
    {PDDI_SP_UVMODE , SHADE_INT(&pglMat::SetUVMode)},
    {PDDI_SP_FILTER , SHADE_INT(&pglMat::SetFilterMode)},
    {PDDI_SP_SHADEMODE , SHADE_INT(&pglMat::SetShadeMode)},
    {PDDI_SP_ISLIT , SHADE_INT(&pglMat::EnableLighting)},
    {PDDI_SP_BLENDMODE , SHADE_INT(&pglMat::SetBlendMode)},
    {PDDI_SP_ALPHATEST , SHADE_INT(&pglMat::EnableAlphaTest)},
    {PDDI_SP_ALPHACOMPARE , SHADE_INT(&pglMat::SetAlphaCompare)},
    {PDDI_SP_TWOSIDED , SHADE_INT(&pglMat::SetTwoSided)},
    {PDDI_SP_EMISSIVEALPHA , SHADE_INT(&pglMat::SetEmissiveAlpha)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pglMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pglMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pglMat::SetAlphaRef)},
    {PDDI_SP_NULL , NULL}
};

GLenum filterMagTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR
};

// GL_NEAREST_MIPMAP_LINEAR not used
GLenum filterMinTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,//GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR,//GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR,//GL_LINEAR_MIPMAP_LINEAR
};

GLenum uvTable[3] =
{
    GL_REPEAT,
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_EDGE
};

GLenum alphaCompareTable[8] =
{
    GL_NEVER,
    GL_ALWAYS,
    GL_LESS,
    GL_LEQUAL,
    GL_GREATER,
    GL_GEQUAL,
    GL_EQUAL,
    GL_NOTEQUAL
};

GLenum alphaBlendTable[8][3] =
{
    { GL_FUNC_ADD, GL_ONE, GL_ZERO },                       //PDDI_BLEND_NONE,
    { GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA },  //PDDI_BLEND_ALPHA
    { GL_FUNC_ADD, GL_ONE, GL_ONE },                        //PDDI_BLEND_ADD
    { GL_FUNC_REVERSE_SUBTRACT, GL_ONE, GL_ONE },           //PDDI_BLEND_SUBTRACT
    { GL_FUNC_ADD, GL_DST_COLOR, GL_ZERO },                 //PDDI_BLEND_MODULATE,
    { GL_FUNC_ADD, GL_DST_COLOR, GL_SRC_COLOR},             //PDDI_BLEND_MODULATE2,
    { GL_FUNC_ADD, GL_ONE, GL_SRC_ALPHA},                   //PDDI_BLEND_ADDMODULATEALPHA,
    { GL_FUNC_REVERSE_SUBTRACT, GL_SRC_ALPHA, GL_SRC_ALPHA} //PDDI_BLEND_SUBMODULATEALPHA
};

static inline void FillGLColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}

pglMat::pglMat(pglContext* c) 
{
    context = c;

    for(int i = 0; i < pglMaxPasses; i++)
    {
        texEnv[i].enabled = false;
        texEnv[i].texture = NULL;
        texEnv[i].uvSet = i;
        texEnv[i].texGen = PDDI_TEXGEN_NONE;
        texEnv[i].uvMode = PDDI_UV_CLAMP;
        texEnv[i].filterMode = PDDI_FILTER_BILINEAR;

        texEnv[i].lit = false;
        texEnv[i].twoSided = false;
        texEnv[i].shadeMode = PDDI_SHADE_GOURAUD;
        texEnv[i].ambient.Set(255,255,255);
        texEnv[i].diffuse.Set(255,255,255);
        texEnv[i].specular.Set(0,0,0);
        texEnv[i].emissive.Set(0,0,0);
        texEnv[i].shininess = 0.0f;

    //   srcBlend = PDDI_BF_ONE;
    //   destBlend = PDDI_BF_ZERO;

        texEnv[i].alphaTest = false;
        texEnv[i].alphaCompareMode = PDDI_COMPARE_GREATEREQUAL;
        texEnv[i].alphaBlendMode = PDDI_BLEND_NONE;
        texEnv[i].alphaRef = 0.5f;
    }
    texEnv[0].enabled = true;
    pass = 0;
    program = 0;
    modelview = projection = sampler = -1;
}

pglMat::~pglMat() 
{
    glDeleteProgram(program);

    for(int i = 0; i < pglMaxPasses; i++)
        if(texEnv[i].texture)
            texEnv[i].texture->Release();
}


const char* pglMat::GetType(void)
{
    static char simple[] = "simple";
    return simple;
}

int pglMat::GetPasses(void)
{
    return 1;
}

void pglMat::SetPass(int pass)
{
    SetDevPass(pass);
}

void pglMat::SetTexture(pddiTexture* t) 
{
    if(t == texEnv[pass].texture)
        return;

    if(texEnv[pass].texture)
        texEnv[pass].texture->Release();

    texEnv[pass].texture = (pglTexture*)t;

    if(texEnv[pass].texture)
        texEnv[pass].texture->AddRef();
}

void pglMat::SetUVMode(int mode) 
{
    texEnv[pass].uvMode = (pddiUVMode)mode;
}

void pglMat::SetFilterMode(int mode) 
{
    texEnv[pass].filterMode = (pddiFilterMode)mode;
}

void pglMat::SetShadeMode(int shade) 
{
    texEnv[pass].shadeMode = (pddiShadeMode)shade;
}

void pglMat::SetTwoSided(int b)
{
    texEnv[pass].twoSided = b != 0;
}

void pglMat::EnableLighting(int b)
{
    texEnv[pass].lit = b != 0;
}

void pglMat::SetAmbient(pddiColour a) 
{
    texEnv[pass].ambient = a;
}

void pglMat::SetDiffuse(pddiColour colour) 
{
    texEnv[pass].diffuse = colour;
}

void pglMat::SetSpecular(pddiColour c) 
{
    texEnv[pass].specular = c;
}

void pglMat::SetEmissive(pddiColour c) 
{
    texEnv[pass].emissive = c;
    SetEmissiveAlpha(c.Alpha());
}

void pglMat::SetEmissiveAlpha(int alpha)
{
    texEnv[pass].diffuse.SetAlpha(alpha);
    if(alpha < 255)
    {
        texEnv[pass].specular.SetAlpha(0);
        texEnv[pass].ambient.SetAlpha(0);
        texEnv[pass].emissive.SetAlpha(0);
    }
    else
    {
        texEnv[pass].specular.SetAlpha(255);
        texEnv[pass].ambient.SetAlpha(255);
        texEnv[pass].emissive.SetAlpha(255);
    }
}

void pglMat::SetShininess(float power) 
{
    texEnv[pass].shininess = power;
}

void pglMat::SetBlendMode(int mode) 
{
    texEnv[pass].alphaBlendMode = (pddiBlendMode)mode;
}

void pglMat::EnableAlphaTest(int b) 
{
    texEnv[pass].alphaTest = b != 0;
}

void pglMat::SetAlphaCompare(int compare) 
{
    texEnv[pass].alphaCompareMode = pddiCompareMode(compare);
}

void pglMat::SetAlphaRef(float ref) 
{
    texEnv[pass].alphaRef = ref;
}

int pglMat::CountDevPasses(void) 
{
    return 1;
}

void pglMat::SetDevPass(unsigned pass)
{
    MICROPROFILE_SCOPEI( "PDDI", "pglMat::SetDevPass", MP_RED );

    if(!program)
    {
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        CompileShader(vertexShader,
            "precision highp float;\n"
            "attribute vec3 position;\n"
            "attribute vec3 normal;\n"
            "attribute vec2 texcoord;\n"
            "attribute vec4 color;\n"

            "uniform mat4 projection;\n"
            "uniform mat4 modelview;\n"

            "varying vec2 tc;\n"
            "varying vec3 fn;\n"
            "varying vec4 fc;\n"
            "varying vec3 vertPos;\n"

            "void main() {\n"
            "    tc = texcoord;\n"
            "    fn = normal;\n"
            "    fc = color;\n"
            "    vec4 vertPos4 = modelview * vec4(position, 1.0);\n"
            "    vertPos = vertPos4.xyz / vertPos4.w;\n"
            "    gl_Position = projection * vertPos4;\n"
            "}\n"
        );

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        CompileShader(fragmentShader,
            "precision mediump float;\n"
            "varying vec2 tc;\n"
            "varying vec3 fn;\n"
            "varying vec4 fc;\n"
            "varying vec3 vertPos;\n"

            "void main() {\n"
            "    gl_FragColor = fc;\n"
            "}\n"
        );

        GLuint textureShader = glCreateShader(GL_FRAGMENT_SHADER);
        CompileShader(textureShader,
            "precision mediump float;\n"
            "varying vec2 tc;\n"
            "varying vec3 fn;\n"
            "varying vec4 fc;\n"
            "varying vec3 vertPos;\n"

            "uniform sampler2D sampler;\n"

            "void main() {\n"
            "    gl_FragColor = texture2D(sampler, tc) * fc;\n"
            "}\n"
        );

        program = glCreateProgram();
        glBindAttribLocation(program, 0, "position");
        glBindAttribLocation(program, 1, "normal");
        glBindAttribLocation(program, 2, "texcoord");
        glBindAttribLocation(program, 3, "color");

        if(!LinkProgram(program, vertexShader, fragmentShader))
        {
            glDeleteProgram(program);
            program = 0;
        }

        textured = glCreateProgram();
        glBindAttribLocation(textured, 0, "position");
        glBindAttribLocation(textured, 1, "normal");
        glBindAttribLocation(textured, 2, "texcoord");
        glBindAttribLocation(textured, 3, "color");

        if(!LinkProgram(textured, vertexShader, textureShader))
        {
            glDeleteProgram(textured);
            textured = 0;
        }

        PDDIASSERT(program);
        PDDIASSERT(textured);

        // Don't leak shaders
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteShader(textureShader);
    }

    if(modelview < 0)
        modelview = glGetUniformLocation(textured, "modelview");
    if(projection < 0)
        projection = glGetUniformLocation(textured, "projection");
    if(sampler < 0)
        sampler = glGetUniformLocation(textured, "sampler");

    int i = 0;

    if(texEnv[i].texture)
    {
        texEnv[i].texture->SetGLState();

        glUseProgram(textured);

        glUniform1i(sampler, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterMagTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterMinTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, uvTable[texEnv[i].uvMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, uvTable[texEnv[i].uvMode]);
    }
    else
    {
        glUseProgram(program);
    }

    if(texEnv[i].alphaTest)
    {
        // TODO
    }

    if(texEnv[i].alphaBlendMode == PDDI_BLEND_NONE)
    {
        glDisable(GL_BLEND);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendEquation(alphaBlendTable[texEnv[i].alphaBlendMode][0]);
        glBlendFunc(alphaBlendTable[texEnv[i].alphaBlendMode][1],alphaBlendTable[texEnv[i].alphaBlendMode][2]);
    }
 
    if(texEnv[i].lit)
    {
        float c[4];
        FillGLColour(texEnv[i].ambient, c);
        FillGLColour(texEnv[i].emissive, c);
        FillGLColour(texEnv[i].diffuse, c);
        FillGLColour(texEnv[i].specular, c);
    }
    else
    {
    }

    if( texEnv[i].twoSided || context->GetCullMode() == PDDI_CULL_NONE )
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
    }

    glUniformMatrix4fv(modelview, 1, GL_FALSE, context->GetMatrix(PDDI_MATRIX_MODELVIEW)->m[0]);
    glUniformMatrix4fv(projection, 1, GL_FALSE, context->GetProjectionMatrix()->m[0]);
}

bool pglMat::CompileShader(GLuint shader, const char* source)
{
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);

    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);
        
        // We don't need the shader anymore.
        glDeleteShader(shader);

        SDL_Log("Shader compilation error: %s", infoLog.data());
        return false;
    }
    return true;
}

bool pglMat::LinkProgram(GLuint program, GLuint vertexShader, GLuint fragmentShader)
{
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

    // Always detach shaders after a successful link
    if(vertexShader)
        glDetachShader(program, vertexShader);
    if(fragmentShader)
        glDetachShader(program, fragmentShader);
    return true;
}

