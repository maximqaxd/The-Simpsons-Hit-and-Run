//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR texture
//=============================================================================

#include <pddi/pvr/pvr.hpp>
#include <pddi/pvr/pvrtex.hpp>
#include <pddi/pvr/pvrcon.hpp>
#include <pddi/pvr/pvrutil.hpp>
#include <pddi/base/debug.hpp>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>

static inline bool IsPow2U32(uint32_t v) { return v && ((v & (v - 1u)) == 0u); }
static inline int AlignUp32Pixels(int w) { return (w + 31) & ~31; }

pvrTexture::pvrTexture(pvrContext* c)
    : context(c)
    , contextID(0)
    , log2X(0)
    , log2Y(0)
    , xSize(0)
    , ySize(0)
    , stridePixels(0)
    , type(PDDI_TEXTYPE_RGB)
    , nMipMap(0)
    , valid(false)
    , vramPtr(0)
    , pvrTxrFormat(0)
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
    if (vramPtr)
    {
        pvr_mem_free(vramPtr);
        vramPtr = 0;
    }
    context->Release();
}

bool pvrTexture::Create(int xSize_, int ySize_, int bpp, int alphaDepth, int nMip,
                        pddiTextureType type_, pddiTextureUsageHint usageHint)
{
    (void)bpp;
    (void)usageHint;

    xSize = xSize_;
    ySize = ySize_;
    type = type_;
    nMipMap = nMip;
    valid = true;
    vramPtr = 0;

    // Decide the staging pixel format / PVR texture format.
    // Keep it simple for now: 16-bit textures only, non-twiddled.
    pddiPixelFormat pf = PDDI_PIXEL_RGB565;
    int baseFmt = PVR_TXRFMT_RGB565;

    if (alphaDepth > 0)
    {
        // Punch-through alpha works best with ARGB1555; smooth alpha with ARGB4444.
        // We default to ARGB4444 when alphaDepth > 1.
        if (alphaDepth <= 1)
        {
            pf = PDDI_PIXEL_ARGB1555;
            baseFmt = PVR_TXRFMT_ARGB1555;
        }
        else
        {
            pf = PDDI_PIXEL_ARGB4444;
            baseFmt = PVR_TXRFMT_ARGB4444;
        }
    }

    lock = pddiLockInfo();
    lock.width = xSize;
    lock.height = ySize;
    lock.depth = 16;
    lock.volDepth = 1;
    stridePixels = xSize;
    const bool pow2W = IsPow2U32((uint32_t)xSize);
    if (!pow2W)
        stridePixels = AlignUp32Pixels(xSize);

    lock.pitch = stridePixels * 2;
    lock.slice = 0;
    lock.bits = NULL;
    pvrutil::FillLockInfoForFormat(pf, &lock);

    const uint32_t strideFlag = pow2W ? PVR_TXRFMT_POW2_STRIDE : PVR_TXRFMT_X32_STRIDE;
    pvrTxrFormat = baseFmt | PVR_TXRFMT_NONTWIDDLED | (int)strideFlag;

    const int mipCount = nMipMap + 1;
    bits = new char*[mipCount];
    memset(bits, 0, sizeof(char*) * (size_t)mipCount);

    return true;
}

pddiPixelFormat pvrTexture::GetPixelFormat()
{
    return lock.format;
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
    switch (lock.format)
    {
        case PDDI_PIXEL_ARGB1555: return 1;
        case PDDI_PIXEL_ARGB4444: return 4;
        default: return 0;
    }
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

    // Allocate staging buffer for mip0 on demand.
    if (!bits || mipLevel < 0 || mipLevel > nMipMap)
        return NULL;

    if (!bits[mipLevel])
    {
        const size_t sizeBytes = pvrutil::AlignUp32((size_t)stridePixels * (size_t)ySize * 2u);
        bits[mipLevel] = (char*)memalign(32, sizeBytes);
        if (!bits[mipLevel])
            return NULL;
        memset(bits[mipLevel], 0, sizeBytes);
    }

    lock.bits = bits[mipLevel];
    return &lock;
}

void pvrTexture::Unlock(int mipLevel)
{
    (void)mipLevel;

    if (!bits || mipLevel < 0 || mipLevel > nMipMap)
        return;

    if (!bits[mipLevel])
        return;

    const size_t sizeBytes = pvrutil::AlignUp32((size_t)stridePixels * (size_t)ySize * 2u);

    if (!vramPtr)
    {
        vramPtr = pvr_mem_malloc(sizeBytes);
        if (!vramPtr)
            return;
    }

    // Flush data cache and upload to VRAM.
    dcache_flush_range((uintptr_t)bits[mipLevel], sizeBytes);
    pvr_txr_load(bits[mipLevel], vramPtr, sizeBytes);
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
