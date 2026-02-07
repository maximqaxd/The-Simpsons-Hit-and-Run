//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR texture
//=============================================================================

#ifndef _PVRTEX_HPP_
#define _PVRTEX_HPP_

#include <pddi/pddi.hpp>
#include <pddi/pdditype.hpp>
#include <pddi/pvr/pvr.hpp>

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

    // PVR accessors
    pvr_ptr_t GetVramPtr() const { return vramPtr; }
    int GetPvrTxrFormat() const { return pvrTxrFormat; }
    int GetStridePixels() const { return stridePixels; }

protected:
    pvrContext* context;
    unsigned contextID;

    int log2X, log2Y;
    int xSize, ySize;
    int stridePixels; // for PVR_TXRFMT_*_STRIDE 
    pddiTextureType type;
    int nMipMap;

    bool valid;
    // PVR: VRAM pointer and format flags used by polygon contexts
    pvr_ptr_t vramPtr;
    int pvrTxrFormat;
    int priority;

    pddiLockInfo lock;

    char** bits; // SH4-side staging buffers for each mip (currently mip0 only)
};

#endif /* _PVRTEX_HPP_ */
