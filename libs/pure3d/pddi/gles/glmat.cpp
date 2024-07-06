//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glmat.hpp>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/glprog.hpp>

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
    program = new pglProgram();
    program->AddRef();
    textured = new pglProgram();
    textured->AddRef();
}

pglMat::~pglMat() 
{
    program->Release();
    textured->Release();

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

    if(!program->GetProgram() || !textured->GetProgram())
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
            "uniform mat4 normalmatrix;\n"

            // Lights
            "uniform struct LightParams {\n"
            "    int enabled;\n"
            "    vec4 position;\n"
            "    vec4 colour;\n"
            "    vec3 attenuation;\n"
            "} lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"

            // Scene
            "uniform int lit;\n"
            "uniform vec4 acs;\n"

            // Material
            "uniform vec4 acm;\n"
            "uniform vec4 dcm;\n"
            "uniform vec4 scm;\n"
            "uniform vec4 ecm;\n"
            "uniform float srm;\n"

            "varying vec2 tc;\n"
            "varying vec4 cpri;\n"
            "varying vec4 csec;\n"

            "vec3 direction(vec4 p1, vec4 p2) {\n"
            "    if (p2.w == 0.0 && p1.w != 0.0)\n"
            "        return normalize(p2.xyz);\n"
            "    else if (p1.w == 0.0 && p2.w != 0.0)\n"
            "        return -normalize(p1.xyz);\n"
            "    else\n"
            "        return normalize(p2.xyz - p1.xyz);\n"
            "}\n"

            "float power(float x, float y) { return y != 0.0 ? pow(x,y) : 1.0; }\n"

            "void main() {\n"
            "    vec4 v = modelview * vec4(position, 1.0);\n"
            "    vec3 n = normalize(mat3(normalmatrix) * normal);\n"

            "    vec3 diff = lit > 0 ? ecm.rgb + acm.rgb * acs.rgb : vec3(1.0);\n"
            "    vec3 spec = vec3(0.0);\n"
            "    for (int i = 0; i < " PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "; i++) {\n"
            "        if (lights[i].enabled <= 0) continue;\n"

            "        vec3 VP = direction(v, lights[i].position);\n"
            "        float f = max(dot(n,VP), 0.0) != 0.0 ? 1.0 : 0.0;\n"
            "        vec3 h = normalize(VP + vec3(0.0, 0.0, 1.0));\n"

            "        vec3 k = lights[i].attenuation;\n"
            "        float d = distance(v.xyz, lights[i].position.xyz);\n"
            "        float att = lights[i].position.w != 0.0 ? 1.0 / (k[0] + k[1] * d + k[2] * d * d) : 1.0;\n"

            "        diff += att * max(dot(n,VP), 0.0) * dcm.rgb * lights[i].colour.rgb;\n"
            "        spec += att * f * power(max(dot(n,h),0.0),srm) * scm.rgb * lights[i].colour.rgb;\n"
            "    }\n"

            "    tc = texcoord;\n"
            "    cpri = color * vec4(diff, dcm.a);\n"
            "    csec = vec4(spec, 0.0);\n"
            "    gl_Position = projection * v;\n"
            "}\n"
        );

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        CompileShader(fragmentShader,
            "precision mediump float;\n"
            "varying vec2 tc;\n"
            "varying vec4 cpri;\n"
            "varying vec4 csec;\n"

            "void main() {\n"
            "    gl_FragColor = cpri + csec;\n"
            "}\n"
        );

        GLuint textureShader = glCreateShader(GL_FRAGMENT_SHADER);
        CompileShader(textureShader,
            "precision mediump float;\n"
            "varying vec2 tc;\n"
            "varying vec4 cpri;\n"
            "varying vec4 csec;\n"

            "uniform sampler2D sampler;\n"

            "void main() {\n"
            "    gl_FragColor = texture2D(sampler, tc) * cpri + csec;\n"
            "}\n"
        );

        if(!program->LinkProgram(vertexShader, fragmentShader))
        {
            PDDIASSERT(false);
        }

        if(!textured->LinkProgram(vertexShader, textureShader))
        {
            PDDIASSERT(false);
        }

        // Don't leak shaders
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteShader(textureShader);
    }

    int i = 0;

    if(texEnv[i].texture)
    {
        context->SetShaderProgram(textured);
        textured->SetTextureEnvironment(&texEnv[i]);

        texEnv[i].texture->SetGLState();

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterMagTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterMinTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, uvTable[texEnv[i].uvMode]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, uvTable[texEnv[i].uvMode]);
    }
    else
    {
        context->SetShaderProgram(program);
        program->SetTextureEnvironment(&texEnv[i]);
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
