//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// Dreamcast platform implementation.
//=============================================================================

#include <p3d/platform/dc/platform.hpp>
#include <p3d/utility.hpp>
#include <p3d/memory.hpp>

#include <constants/version.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos/timer.h>


tContextInitData::tContextInitData()
{
    xsize     = 640;
    ysize     = 480;
    bufferMask = PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH;
    PDDIlib[0] = '\0';
}

tPlatform* tPlatform::currentPlatform = NULL;

tPlatform::tPlatform(void* inst)
{
    instance       = inst;
    currentContext = NULL;
    nContexts      = 0;
}

tPlatform::~tPlatform()
{
}

tPlatform* tPlatform::Create(void* instance)
{
    p3d::UsePermanentMem(true);

    if (!currentPlatform)
    {
        currentPlatform = new tPlatform(instance);
        p3d::platform  = currentPlatform;
    }

    p3d::UsePermanentMem(false);
    return currentPlatform;
}

void tPlatform::Destroy(tPlatform* plat)
{
    P3DASSERT(plat == currentPlatform);
    delete currentPlatform;
    currentPlatform = NULL;
}

tContext* tPlatform::CreateContext(tContextInitData* d)
{
    P3DASSERT(nContexts < P3D_MAX_CONTEXTS);

    pddiDevice*        device;
    pddiDisplay*       display;
    pddiRenderContext* context;

    p3d::printf("Pure3D v%s, released %s\n", ATG_VERSION, ATG_RELEASE_DATE);

    p3d::UsePermanentMem(true);

    int success = pddiCreate(PDDI_VERSION_MAJOR, PDDI_VERSION_MINOR, &device);
    if (success != PDDI_OK)
    {
        if (success == PDDI_VERSION_ERROR)
        {
            P3DVERIFY(0, "Cannot initialize PDDI library due to an incorrect version");
        }
        else
        {
            P3DVERIFY(0, "Cannot initialize PDDI library, unknown error.");
        }
    }

    tDebug::CapturePDDIMessages(device);

    display = device->NewDisplay(0);
    display->InitDisplay(d);

    context = device->NewRenderContext(display);
    P3DVERIFY(context != NULL, "NewRenderContext() failed");

    pddiLibInfo libInfo;
    device->GetLibraryInfo(&libInfo);

    p3d::printf("tContext created: PDDI v%d.%d (build %d) '%s'\n",
                libInfo.versionMajor,
                libInfo.versionMinor,
                libInfo.versionBuild,
                libInfo.description);

    for (int find = 0; find < P3D_MAX_CONTEXTS; find++)
    {
        if (!contexts[find].context)
        {
            contexts[find].context = new tContext(device, display, context);
            contexts[find].pddiLib = NULL;

            if (!currentContext)
                SetActiveContext(contexts[find].context);

            contexts[find].context->Setup();

            nContexts++;

            p3d::UsePermanentMem(false);

            return contexts[find].context;
        }
    }

    p3d::UsePermanentMem(false);

    return NULL;
}

void tPlatform::DestroyContext(tContext* context)
{
    int foundHandle = -1;
    for (int i = 0; i < nContexts; i++)
        if (contexts[i].context == context)
            foundHandle = i;

    P3DASSERT(foundHandle != -1);

    context->Shutdown();

    contexts[foundHandle].context = NULL;
    contexts[foundHandle].pddiLib = NULL;
    delete context;
    nContexts--;

    if (currentContext == context)
    {
        currentContext = NULL;
    }
}

void tPlatform::SetActiveContext(tContext* context)
{
    P3DASSERT(context);
    currentContext   = context;
    p3d::context     = context;
    p3d::inventory   = context->GetInventory();
    p3d::stack       = context->GetMatrixStack();
    p3d::loadManager = context->GetLoadManager();
    p3d::pddi       = context->GetContext();
    p3d::device     = context->GetDevice();
    p3d::display    = context->GetDisplay();
}

P3D_U64 tPlatform::GetTimeFreq(void)
{
    return 1000;
}

P3D_U64 tPlatform::GetTime(void)
{
    return (P3D_U64)timer_ms_gettime64();
}

tPlatform* tPlatform::GetPlatform(void)
{
    return currentPlatform;
}
