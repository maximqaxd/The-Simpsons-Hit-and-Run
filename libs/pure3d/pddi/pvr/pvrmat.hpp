//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR material/shader
//=============================================================================

#ifndef _PVRMAT_HPP_
#define _PVRMAT_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/baseshader.hpp>
#include <pddi/pvr/pvrcon.hpp>

class pvrTexture;

const int pvrMaxPasses = 1;

struct pvrTextureEnv
{
    bool enabled;
    pvrTexture* texture;

    int uvSet;
    pddiTextureGen texGen;
    pddiUVMode uvMode;
    pddiFilterMode filterMode;

    bool alphaTest;
    pddiBlendMode alphaBlendMode;
    pddiCompareMode alphaCompareMode;
    float alphaRef;

    bool lit;
    bool twoSided;
    pddiShadeMode shadeMode;
    pddiColour diffuse;
    pddiColour specular;
    pddiColour ambient;
    pddiColour emissive;
    float shininess;
};

class pvrMat : public pddiBaseShader
{
public:
    pvrMat(pvrContext*);
    ~pvrMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];
    static pddiShadeIntTable intTable[];
    static pddiShadeFloatTable floatTable[];

    const char* GetType(void);
    int GetPasses(void);
    void SetPass(int pass);

    pddiShadeTextureTable* GetTextureTable(void) { return textureTable; }
    pddiShadeIntTable* GetIntTable(void) { return intTable; }
    pddiShadeFloatTable* GetFloatTable(void) { return floatTable; }
    pddiShadeColourTable* GetColourTable(void) { return colourTable; }

    void SetTexture(pddiTexture* texture);
    void SetUVMode(int mode);
    void SetFilterMode(int mode);

    void SetShadeMode(int shade);
    void SetTwoSided(int);

    void EnableLighting(int);

    void SetDiffuse(pddiColour colour);
    void SetAmbient(pddiColour colour);
    void SetEmissive(pddiColour);
    void SetEmissiveAlpha(int);
    void SetSpecular(pddiColour);
    void SetShininess(float power);

    void SetBlendMode(int mode);
    void EnableAlphaTest(int);
    void SetAlphaCompare(int compare);
    void SetAlphaRef(float ref);

    int CountDevPasses(void);
    void SetDevPass(unsigned);

    const pvrTextureEnv& GetTextureEnv(int p = 0) const { return texEnv[p]; }

private:
    pvrContext* context;
    int pass;
    pvrTextureEnv texEnv[pvrMaxPasses];
};

#endif /* _PVRMAT_HPP_ */
