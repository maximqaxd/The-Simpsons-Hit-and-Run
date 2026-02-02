//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR device 
//=============================================================================

#include <pddi/pvr/pvr.hpp>
#include <pddi/pvr/pvrdev.hpp>
#include <pddi/pvr/pvrdisplay.hpp>
#include <pddi/pvr/pvrcon.hpp>
#include <pddi/pvr/pvrtex.hpp>
#include <pddi/pvr/pvrmat.hpp>

#include <pddi/base/debug.hpp>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PDDI_PVR_BUILD 1

static pvrDevice gblDevice;

static char libName[] = "PowerVR CLX2 (KallistiOS)";

int pddiCreate(int versionMajor, int versionMinor, pddiDevice** device)
{
    if ((versionMajor != PDDI_VERSION_MAJOR) || (versionMinor != PDDI_VERSION_MINOR))
    {
        *device = NULL;
        return PDDI_VERSION_ERROR;
    }

    *device = &gblDevice;
    return PDDI_OK;
}

//--------------------------------------------------------------
pvrDevice::pvrDevice()
{
    context = NULL;
    initialized = false;
}

//--------------------------------------------------------------
pvrDevice::~pvrDevice()
{
}

//--------------------------------------------------------------
void pvrDevice::GetLibraryInfo(pddiLibInfo* info)
{
    info->versionMajor = PDDI_VERSION_MAJOR;
    info->versionMinor = PDDI_VERSION_MINOR;
    info->versionBuild = PDDI_PVR_BUILD;
    info->libID = PDDI_LIBID_OPENGL; // reuse or add PDDI_LIBID_PVR if needed
    strcpy(info->description, libName);
}

//--------------------------------------------------------------
unsigned pvrDevice::GetCaps()
{
    return 0;
}

//--------------------------------------------------------------
const char* pvrDevice::GetDeviceDescription()
{
    return libName;
}

//--------------------------------------------------------------
void pvrDevice::SetCurrentContext(pddiRenderContext* c)
{
    context = c;
}

//--------------------------------------------------------------
pddiRenderContext* pvrDevice::GetCurrentContext(void)
{
    return context;
}

//--------------------------------------------------------------
pddiDisplay* pvrDevice::NewDisplay(int id)
{
    (void)id;
    pvrDisplay* display = new pvrDisplay();
    if (display->GetLastError() != PDDI_OK)
    {
        delete display;
        return NULL;
    }
    return (pddiDisplay*)display;
}

//--------------------------------------------------------------
pddiRenderContext* pvrDevice::NewRenderContext(pddiDisplay* display)
{
    pvrContext* ctx = new pvrContext(this, (pvrDisplay*)display);
    if (ctx->GetLastError() != PDDI_OK)
    {
        delete ctx;
        return NULL;
    }
    return ctx;
}

//--------------------------------------------------------------
pddiTexture* pvrDevice::NewTexture(pddiTextureDesc* desc)
{
    pvrTexture* tex = new pvrTexture((pvrContext*)context);
    if (!tex->Create(desc->GetSizeX(), desc->GetSizeY(), desc->GetBitDepth(),
                    desc->GetAlphaDepth(), desc->GetMipMapCount(),
                    desc->GetType(), desc->GetUsage()))
    {
        lastError = tex->GetLastError();
        delete tex;
        return NULL;
    }
    return tex;
}

//--------------------------------------------------------------
pddiShader* pvrDevice::NewShader(const char* name, const char* aux)
{
    pvrMat* mat = new pvrMat((pvrContext*)context);
    if (mat->GetLastError() != PDDI_OK)
    {
        delete mat;
        return NULL;
    }
    return mat;
}

//--------------------------------------------------------------
pddiPrimBuffer* pvrDevice::NewPrimBuffer(pddiPrimBufferDesc* desc)
{
    return new pvrPrimBuffer((pvrContext*)context,
                            desc->GetPrimType(),
                            desc->GetVertexFormat(),
                            desc->GetVertexCount(),
                            desc->GetIndexCount());
}

//--------------------------------------------------------------
void pvrDevice::AddCustomShader(const char* name, const char* aux)
{
    (void)name;
    (void)aux;
}

//--------------------------------------------------------------
void pvrDevice::Release(void)
{
}
