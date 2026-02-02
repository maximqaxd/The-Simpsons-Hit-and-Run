//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// Dreamcast platform
//=============================================================================

#ifndef _PLATFORM_HPP
#define _PLATFORM_HPP

#include <p3d/buildconfig.hpp>
#include <p3d/platform/dc/plat_types.hpp>
#include <pddi/pddi.hpp>

#include <malloc.h>

static const int P3D_MAX_CONTEXTS = 4;

class tContext;
class tFile;
class tPolySkinLoader;
class tTask;

class tContextInitData : public pddiDisplayInit
{
public:
    char PDDIlib[128];  

    tContextInitData();
};

class tPlatformContext
{
public:
    tContext* context;
    void*     pddiLib;

    tPlatformContext() { context = NULL; pddiLib = 0; }
};

class tPlatform
{
public:
    // platform creation/destruction
    static tPlatform* Create(void* instance);
    static void Destroy(tPlatform*);
    static tPlatform* GetPlatform(void);

    // context creation/destruction
    tContext* CreateContext(tContextInitData*);
    void DestroyContext(tContext*);

    // active context control
    void SetActiveContext(tContext*);
    tContext* GetActiveContext(void) { return currentContext; }

    // Time
    P3D_U64 GetTimeFreq(void);
    P3D_U64 GetTime(void);

private:
    tFile* OpenFile(const char* filename);

private:
    tPlatform(void* instance);
    ~tPlatform();

    static tPlatform* InternalCreate(void* instance);
    static tPlatform* currentPlatform;

    void*     instance;
    tContext* currentContext;

    int              nContexts;
    tPlatformContext contexts[P3D_MAX_CONTEXTS];
};

#endif
