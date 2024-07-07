//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/base/debug.hpp>
#include <SDL.h>

#include<stdio.h>
#include<string.h>
#include<math.h>

bool pglDisplay::CheckExtension( const char *extName )
{
    return SDL_GL_ExtensionSupported(extName) == SDL_TRUE;
}

pglDisplay ::pglDisplay(pddiDisplayInfo* info)
{
    displayInfo = info;
    mode = PDDI_DISPLAY_WINDOW;
    winWidth = 640;
    winHeight = 480;
    winBitDepth = 32;

    context = NULL;

    win = NULL;
    hRC = NULL;
    prevRC = NULL;

    extBGRA = false;

    gammaR = gammaG = gammaB = 1.0f;

    reset = true;
	m_ForceVSync = false;
}

pglDisplay ::~pglDisplay()
{
    /* release and free the device context and rendering context */
    vglEnd();
    SDL_SetWindowGammaRamp(win, initialGammaRamp[0], initialGammaRamp[1], initialGammaRamp[2]);
}

#define KEYPRESSED(x) (GetKeyState((x)) & (1<<(sizeof(int)*8)-1))

long pglDisplay ::ProcessWindowMessage(SDL_Window* win, const SDL_WindowEvent* event)
{
    switch (event->event)
    {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            SDL_GL_GetDrawableSize( win, &winWidth, &winHeight );
            break;

        case SDL_WINDOWEVENT_CLOSE:
            /* release and free the device context and rendering context */
            SDL_GL_DeleteContext(hRC);
            break;

		default:
            return 0;
     }
     /* return 1 if handled message, 0 if not */

     return 1;
}


void pglDisplay ::SetWindow(SDL_Window* wnd)
{
    SDL_GetWindowGammaRamp(wnd, initialGammaRamp[0], initialGammaRamp[1], initialGammaRamp[2]);
    win = wnd;
}

bool pglDisplay ::InitDisplay(int x, int y, int bpp)
{
    // check we are not trying to init to the same resolution
    if((x == winWidth) &&  (y == winHeight) && (bpp == winBitDepth))
    {
        return true;
    }

    // fill in the relevent portions of the casced display init structure
    displayInit.xsize = x;
    displayInit.ysize = y;
    displayInit.bpp = bpp;

    // do the full init
    return InitDisplay(&displayInit);
}

bool pglDisplay ::InitDisplay(const pddiDisplayInit* init)
{
    extBGRA = true;
    return vglInit(0x800000);
}

pddiDisplayInfo* pglDisplay ::GetDisplayInfo(void)
{
    return displayInfo;
}

unsigned pglDisplay ::GetFreeTextureMem()
{
    return unsigned(-1);
}

unsigned pglDisplay ::GetBufferMask()
{
    return unsigned(-1);
}

int pglDisplay ::GetHeight()
{
    return winHeight;
}

int pglDisplay ::GetWidth()
{
    return winWidth;
}

int pglDisplay::GetDepth()
{
    return winBitDepth;
}

pddiDisplayMode pglDisplay::GetDisplayMode(void)
{
    return mode;
}

int pglDisplay::GetNumColourBuffer(void)
{
    return 2;
}

void pglDisplay::GetGamma(float* r, float* g, float* b)
{
    *r = gammaR;
    *g = gammaG;
    *b = gammaB;
}

void pglDisplay::SetGamma(float r, float g, float b)
{
    gammaR = r;
    gammaG = g;
    gammaB = b;

    Uint16 gamma[3][256];

    double igr = 1.0 / (double)r;
    double igg = 1.0 / (double)g;
    double igb = 1.0 / (double)b;

    const double n = 1.0 / 65535.0;

    for(int i=0; i < 256; i++)
    {
        double gcr = pow((double)initialGammaRamp[0][i]   * n, igr);
        double gcg = pow((double)initialGammaRamp[1][i] * n, igg);
        double gcb = pow((double)initialGammaRamp[2][i]  * n, igb);

        gamma[0][i] =   (Uint16)(65535.0 * ((1.0 < gcr) ? 1.0 : gcr));
        gamma[1][i] = (Uint16)(65535.0 * ((1.0 < gcg) ? 1.0 : gcg));
        gamma[2][i] =  (Uint16)(65535.0 * ((1.0 < gcb) ? 1.0 : gcb));
    }

    SDL_SetWindowGammaRamp(win, gamma[0], gamma[1], gamma[2]);
}

void pglDisplay::SwapBuffers(void)
{
    vglSwapBuffers(GL_FALSE);
    reset = false;
}

    
unsigned pglDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    // not implemented under vita
    assert( 0 && "PDDI: pddiDisplay::ScreenShot() - Not implemented under vita." );
    return 0;
}

unsigned pglDisplay::FillDisplayModes(int displayIndex, pddiModeInfo* displayModes)
{
    int nModes = 0;

    SDL_DisplayMode devMode;

    for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++)
    {
        if(SDL_GetDisplayMode(displayIndex, i, &devMode) == 0)
        {
            displayModes[nModes].width = devMode.w;
            displayModes[nModes].height = devMode.h;
            displayModes[nModes].bpp = 32;
            nModes++;
        }
    }

    return nModes;
}

void pglDisplay::BeginTiming()
{
    beginTime = (float)SDL_GetTicks();
}

float pglDisplay::EndTiming()
{
    return (float)SDL_GetTicks() - beginTime;
}

void pglDisplay::BeginContext(void)
{
}

void pglDisplay::EndContext(void)
{
}

