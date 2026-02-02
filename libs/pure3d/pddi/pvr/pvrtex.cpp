//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR texture
//=============================================================================

#include <pddi/pvr/pvr.hpp>
#include <pddi/pvr/pvrtex.hpp>
#include <pddi/pvr/pvrcon.hpp>
#include <pddi/base/debug.hpp>
#include <string.h>
#include <stdlib.h>

pvrTexture::pvrTexture(pvrContext* c)
    : context(c)
    , contextID(0)
    , log2X(0)
    , log2Y(0)
    , xSize(0)
    , ySize(0)
    , type(PDDI_TEXTYPE_RGB)
    , nMipMap(0)
    , valid(false)
    , vramPtr(NULL)
    , priority(0)
    , bits(NULL)
{
    context->AddRef();
    memset(&lock, 0, sizeof(lock));
}

pvrTexture::~pvrTexture()
{
    if (bits)
    {
        for (int i = 0; i < nMipMap + 1; i++)
            if (bits[i])
                free(bits[i]);
        delete[] bits;
        bits = NULL;
    }
    // PVR: pvr_mem_free(vramPtr);
    context->Release();
}

bool pvrTexture::Create(int xSize_, int ySize_, int bpp, int alphaDepth, int nMip,
                        pddiTextureType type_, pddiTextureUsageHint usageHint)
{
    (void)bpp;
    (void)alphaDepth;
    (void)usageHint;

    xSize = xSize_;
    ySize = ySize_;
    type = type_;
    nMipMap = nMip;
    valid = true;
    vramPtr = NULL;

    // PVR: pvr_mem_malloc(), pvr_txr_load() on upload
    return true;
}

pddiPixelFormat pvrTexture::GetPixelFormat()
{
    return PDDI_PIXEL_RGB565;
}

int pvrTexture::GetWidth()
{
    return xSize;
}

int pvrTexture::GetHeight()
{
    return ySize;
}

int pvrTexture::GetDepth()
{
    return 0;
}

int pvrTexture::GetNumMipMaps()
{
    return nMipMap;
}

int pvrTexture::GetAlphaDepth()
{
    return 0;
}

int pvrTexture::GetNumPaletteEntries(void)
{
    return 0;
}

void pvrTexture::SetPalette(int nEntries, pddiColour* palette)
{
    (void)nEntries;
    (void)palette;
}

int pvrTexture::GetPalette(pddiColour* palette)
{
    (void)palette;
    return 0;
}

pddiLockInfo* pvrTexture::Lock(int mipLevel, pddiRect* rect)
{
    (void)mipLevel;
    (void)rect;
    return &lock;
}

void pvrTexture::Unlock(int mipLevel)
{
    (void)mipLevel;
    // PVR: convert and pvr_txr_load(lock.bits, vramPtr, size)
}

void pvrTexture::Prefetch(void)
{
}

void pvrTexture::Discard(void)
{
}

void pvrTexture::SetPriority(int p)
{
    priority = p;
}

int pvrTexture::GetPriority()
{
    return priority;
}
