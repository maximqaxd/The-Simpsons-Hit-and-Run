//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR texture
//=============================================================================

#ifndef _PVRTEX_HPP_
#define _PVRTEX_HPP_

#include <pddi/pddi.hpp>
#include <pddi/pdditype.hpp>

class pvrContext;

class pvrTexture : public pddiTexture
{
public:
    pvrTexture(pvrContext*);
    ~pvrTexture();

    bool Create(int xSize, int ySize, int bpp, int alphaDepth, int nMip,
                pddiTextureType type = PDDI_TEXTYPE_RGB,
                pddiTextureUsageHint usageHint = PDDI_USAGE_STATIC);

    pddiPixelFormat GetPixelFormat();
    int GetWidth();
    int GetHeight();
    int GetDepth();
    int GetNumMipMaps();
    int GetAlphaDepth();

    int GetNumPaletteEntries(void);
    void SetPalette(int nEntries, pddiColour* palette);
    int GetPalette(pddiColour* palette);

    pddiLockInfo* Lock(int mipLevel, pddiRect* rect = 0);
    void Unlock(int mipLevel);

    void Prefetch(void);
    void Discard(void);
    void SetPriority(int priority);
    int GetPriority();

protected:
    pvrContext* context;
    unsigned contextID;

    int log2X, log2Y;
    int xSize, ySize;
    pddiTextureType type;
    int nMipMap;

    bool valid;
    // PVR: pvr_ptr_t vram_ptr or equivalent stub
    void* vramPtr;
    int priority;

    pddiLockInfo lock;

    char** bits;
};

#endif /* _PVRTEX_HPP_ */
