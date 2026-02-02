//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR display
//=============================================================================

#include <pddi/pvr/pvr.hpp>
#include <pddi/pvr/pvrdisplay.hpp>
#include <pddi/base/debug.hpp>
#include <string.h>

pvrDisplay::pvrDisplay()
{
    mode = PDDI_DISPLAY_WINDOW;
    winWidth = 640;
    winHeight = 480;
    winBitDepth = 16;
    context = NULL;
    forceVSync = false;
    memset(&displayInit, 0, sizeof(displayInit));
}

pvrDisplay::~pvrDisplay()
{
    context = NULL;
}

bool pvrDisplay::InitDisplay(const pddiDisplayInit* init)
{
    if (!init)
        return false;
    displayInit = *init;
    winWidth = init->xsize;
    winHeight = init->ysize;
    winBitDepth = 16;  
    // PVR: pvr_init_params_t, pvr_init() etc.
    return true;
}

bool pvrDisplay::InitDisplay(int x, int y, int bpp)
{
    winWidth = x;
    winHeight = y;
    winBitDepth = bpp;
    return true;
}

int pvrDisplay::GetHeight(void)
{
    return winHeight;
}

int pvrDisplay::GetWidth(void)
{
    return winWidth;
}

int pvrDisplay::GetDepth(void)
{
    return winBitDepth;
}

pddiDisplayMode pvrDisplay::GetDisplayMode(void)
{
    return mode;
}

int pvrDisplay::GetNumColourBuffer(void)
{
    return 1;
}

unsigned pvrDisplay::GetBufferMask(void)
{
    return PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH;
}

unsigned pvrDisplay::GetFreeTextureMem(void)
{
    // PVR: query TA/free VRAM
    return 0;
}

void pvrDisplay::SwapBuffers(void)
{
    // PVR: scene already finished in EndFrame
}

unsigned pvrDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    (void)buffer;
    (void)nBytes;
    return 0;
}

void pvrDisplay::BeginContext(void)
{
}

void pvrDisplay::EndContext(void)
{
}

void pvrDisplay::SetGamma(float r, float g, float b)
{
    (void)r;
    (void)g;
    (void)b;
}

void pvrDisplay::GetGamma(float* r, float* g, float* b)
{
    if (r) *r = 1.0f;
    if (g) *g = 1.0f;
    if (b) *b = 1.0f;
}
