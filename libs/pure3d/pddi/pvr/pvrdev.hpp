//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR device 
//=============================================================================

#ifndef _PVRDEV_HPP_
#define _PVRDEV_HPP_

#include <pddi/pddi.hpp>

class pvrDevice : public pddiDevice
{
public:
    pvrDevice();
    ~pvrDevice();

    void        GetLibraryInfo(pddiLibInfo* info);
    const char* GetDeviceDescription();
    unsigned    GetCaps();

    void SetCurrentContext(pddiRenderContext* context);
    pddiRenderContext* GetCurrentContext();

    pddiDisplay* NewDisplay(int id);
    pddiRenderContext* NewRenderContext(pddiDisplay* display);
    pddiTexture* NewTexture(pddiTextureDesc* desc);
    pddiPrimBuffer* NewPrimBuffer(pddiPrimBufferDesc* desc);
    pddiShader* NewShader(const char* name, const char* aux = NULL);

    void AddCustomShader(const char* name, const char* aux = NULL);

    void Release(void);

protected:
    bool initialized;
    pddiRenderContext* context;
};

#endif /* _PVRDEV_HPP_ */
