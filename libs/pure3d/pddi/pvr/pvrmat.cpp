//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR material/shader
//=============================================================================

#include <pddi/pvr/pvr.hpp>
#include <pddi/pvr/pvrmat.hpp>
#include <pddi/pvr/pvrtex.hpp>
#include <pddi/base/debug.hpp>
#include <string.h>

pddiShadeColourTable pvrMat::colourTable[] =
{
    { PDDI_SP_AMBIENT,  SHADE_COLOUR(&pvrMat::SetAmbient) },
    { PDDI_SP_DIFFUSE,  SHADE_COLOUR(&pvrMat::SetDiffuse) },
    { PDDI_SP_EMISSIVE, SHADE_COLOUR(&pvrMat::SetEmissive) },
    { PDDI_SP_SPECULAR, SHADE_COLOUR(&pvrMat::SetSpecular) },
    { PDDI_SP_NULL,     NULL }
};

pddiShadeTextureTable pvrMat::textureTable[] =
{
    { PDDI_SP_BASETEX, SHADE_TEXTURE(&pvrMat::SetTexture) },
    { PDDI_SP_NULL,    NULL }
};

pddiShadeIntTable pvrMat::intTable[] =
{
    { PDDI_SP_ALPHATEST,    SHADE_INT(&pvrMat::EnableAlphaTest) },
    { PDDI_SP_BLENDMODE,    SHADE_INT(&pvrMat::SetBlendMode) },
    { PDDI_SP_ALPHACOMPARE, SHADE_INT(&pvrMat::SetAlphaCompare) },
    { PDDI_SP_TWOSIDED,     SHADE_INT(&pvrMat::SetTwoSided) },
    { PDDI_SP_UVMODE,       SHADE_INT(&pvrMat::SetUVMode) },
    { PDDI_SP_FILTER,       SHADE_INT(&pvrMat::SetFilterMode) },
    { PDDI_SP_SHADEMODE,    SHADE_INT(&pvrMat::SetShadeMode) },
    { PDDI_SP_ISLIT,        SHADE_INT(&pvrMat::EnableLighting) },
    { PDDI_SP_EMISSIVEALPHA, SHADE_INT(&pvrMat::SetEmissiveAlpha) },
    { PDDI_SP_NULL,         NULL }
};

pddiShadeFloatTable pvrMat::floatTable[] =
{
    { PDDI_SP_SHININESS,             SHADE_FLOAT(&pvrMat::SetShininess) },
    { PDDI_SP_ALPHACOMPARE_THRESHOLD, SHADE_FLOAT(&pvrMat::SetAlphaRef) },
    { PDDI_SP_NULL,                  NULL }
};

pvrMat::pvrMat(pvrContext* c)
    : context(c)
    , pass(0)
{
    context->AddRef();
    memset(texEnv, 0, sizeof(texEnv));

    // defaults
    texEnv[0].enabled = false;
    texEnv[0].texture = NULL;
    texEnv[0].uvSet = 0;
    texEnv[0].texGen = PDDI_TEXGEN_NONE;
    texEnv[0].uvMode = PDDI_UV_TILE;
    texEnv[0].filterMode = PDDI_FILTER_BILINEAR;

    texEnv[0].alphaTest = false;
    texEnv[0].alphaBlendMode = PDDI_BLEND_NONE;
    texEnv[0].alphaCompareMode = PDDI_COMPARE_ALWAYS;
    texEnv[0].alphaRef = 0.5f;

    texEnv[0].lit = false;
    texEnv[0].twoSided = false;
    texEnv[0].shadeMode = PDDI_SHADE_GOURAUD;

    texEnv[0].diffuse = pddiColour(255, 255, 255, 255);
    texEnv[0].ambient = pddiColour(0, 0, 0, 255);
    texEnv[0].emissive = pddiColour(0, 0, 0, 255);
    texEnv[0].specular = pddiColour(0, 0, 0, 255);
    texEnv[0].shininess = 0.0f;
}

pvrMat::~pvrMat()
{
    context->Release();
}

const char* pvrMat::GetType(void)
{
    return "pvr";
}

int pvrMat::GetPasses(void)
{
    return 1;
}

void pvrMat::SetPass(int p)
{
    pass = p;
}

void pvrMat::SetTexture(pddiTexture* t)
{
    texEnv[pass].texture = (pvrTexture*)t;
    texEnv[pass].enabled = (t != NULL);
}

void pvrMat::SetUVMode(int mode)
{
    texEnv[pass].uvMode = (pddiUVMode)mode;
}

void pvrMat::SetFilterMode(int mode)
{
    texEnv[pass].filterMode = (pddiFilterMode)mode;
}

void pvrMat::SetShadeMode(int shade)
{
    texEnv[pass].shadeMode = (pddiShadeMode)shade;
}

void pvrMat::SetTwoSided(int enabled)
{
    texEnv[pass].twoSided = (enabled != 0);
}

void pvrMat::EnableLighting(int enabled)
{
    texEnv[pass].lit = (enabled != 0);
}

void pvrMat::SetDiffuse(pddiColour c)
{
    texEnv[pass].diffuse = c;
}

void pvrMat::SetAmbient(pddiColour c)
{
    texEnv[pass].ambient = c;
}

void pvrMat::SetEmissive(pddiColour c)
{
    texEnv[pass].emissive = c;
}

void pvrMat::SetEmissiveAlpha(int a)
{
    (void)a;
}

void pvrMat::SetSpecular(pddiColour c)
{
    texEnv[pass].specular = c;
}

void pvrMat::SetShininess(float power)
{
    texEnv[pass].shininess = power;
}

void pvrMat::SetBlendMode(int mode)
{
    texEnv[pass].alphaBlendMode = (pddiBlendMode)mode;
}

void pvrMat::EnableAlphaTest(int enable)
{
    texEnv[pass].alphaTest = (enable != 0);
}

void pvrMat::SetAlphaCompare(int mode)
{
    texEnv[pass].alphaCompareMode = (pddiCompareMode)mode;
}

void pvrMat::SetAlphaRef(float ref)
{
    texEnv[pass].alphaRef = ref;
}

int pvrMat::CountDevPasses(void)
{
    return 1;
}

void pvrMat::SetDevPass(unsigned p)
{
    pass = (int)p;
}
