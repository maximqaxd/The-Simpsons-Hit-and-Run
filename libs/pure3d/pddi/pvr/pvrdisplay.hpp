//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR display
//=============================================================================

#ifndef _PVRDISPLAY_HPP_
#define _PVRDISPLAY_HPP_

#include <pddi/pddi.hpp>

class pvrContext;

class pvrDisplay : public pddiDisplay
{
public:
    pvrDisplay();
    ~pvrDisplay();

    bool InitDisplay(const pddiDisplayInit*);
    bool InitDisplay(int x, int y, int bpp);

    int GetHeight(void);
    int GetWidth(void);
    int GetDepth(void);
    pddiDisplayMode GetDisplayMode(void);
    int GetNumColourBuffer(void);
    unsigned GetBufferMask(void);

    unsigned GetFreeTextureMem(void);

    void SwapBuffers(void);

    unsigned Screenshot(pddiColour* buffer, int nBytes);

    void SetContext(pvrContext* c) { context = c; }
    bool GetForceVSync(void) { return forceVSync; }

    void BeginContext(void);
    void EndContext(void);

    void SetGamma(float r, float g, float b);
    void GetGamma(float* r, float* g, float* b);

private:
    pddiDisplayMode mode;
    int winWidth;
    int winHeight;
    int winBitDepth;

    pddiDisplayInit displayInit;

    pvrContext* context;

    bool forceVSync;
};

#endif /* _PVRDISPLAY_HPP_ */
