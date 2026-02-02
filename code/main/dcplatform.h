//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   DCPlatform   
//
// Description: Abstracts the differences for setting up and shutting down
//              the different platforms.
//
// History:     + Stolen and cleaned up from Penthouse -- Darwin Chau
//
//=============================================================================

#ifndef DCPLATFORM_H
#define DCPLATFORM_H

//========================================
// Nested Includes
//========================================
#include "platform.h" // base class
#include <data/config/gameconfig.h> // interface
#include <kos.h>
//========================================
// Forward References
//========================================
struct IRadMemoryHeap;
class tPlatform;
class tContext;

//=============================================================================
//
// Synopsis:    Provides abstraction for setting up and closing a win32 exe.
//
//=============================================================================

class DCPlatform : public Platform
{
public:

    enum Resolution
    {
        Res_640x480 = 0,
        Res_800x600,
        Res_1024x768,
        Res_1152x864,
        Res_1280x1024,
        Res_1600x1200
    };

public:

    // Static Methods for accessing this singleton.
    static DCPlatform* CreateInstance();
    static DCPlatform* GetInstance();
    static void DestroyInstance();

    // Had to workaround our nice clean design cause FTech must be init'ed
    // before anything else is done.
    static bool InitializeWindow();
    static void InitializeFoundation();
    static void InitializeMemory();
    static void ShutdownMemory();

    // Implement Platform interface.
    virtual void InitializePlatform();
    virtual void ShutdownPlatform();

    virtual void LaunchDashboard();
    virtual void ResetMachine();

    virtual void DisplaySplashScreen( SplashScreen screenID, 
        const char* overlayText = NULL, 
        float fontScale = 1.0f, 
        float textPosX = 0.0f,
        float textPosY = 0.0f,
        tColour textColour = tColour( 255,255,255 ),
        int fadeFrames = 3 );

    virtual void DisplaySplashScreen( const char* textureName,
        const char* overlayText = NULL, 
        float fontScale = 1.0f, 
        float textPosX = 0.0f,
        float textPosY = 0.0f,
        tColour textColour = tColour( 255,255,255 ),
        int fadeFrames = 3 );

    virtual bool OnDriveError( radFileError error, const char* pDriveName, void* pUserData );  
    virtual void OnControllerError(const char *msg);

    // Set the resolution of the display, or increase the size of the window.
    // Returns true if the change was successful, false if resolution is not supported.
    bool SetResolution( Resolution res, int bpp, bool fullscreen );

    Resolution GetResolution() const;
    int GetBPP() const;
    bool IsFullscreen() const;


private:

    // Constructors, Destructors, and Operators
    DCPlatform();
    virtual ~DCPlatform();

    // Unused Constructors, Destructors, and Operators
    DCPlatform( const DCPlatform& aPlatform );
    DCPlatform& operator=( const DCPlatform& aPlatform );

    // Methods from Platform
    virtual void InitializeFoundationDrive();
    virtual void ShutdownFoundation();

    virtual void InitializePure3D();
    virtual void ShutdownPure3D();

    // Initializes the d3d context
    void InitializeContext();

    // Resolution related methods
    static void TranslateResolution( Resolution res, int&x, int&y );
    bool IsResolutionSupported( Resolution res, int bpp ) const;

    // Windows methods.
    void ResizeWindow();
    static void ShowTheCursor( bool show );

private:

    // Pointer to the one and only instance of this singleton.
    static DCPlatform* spInstance;

    // Private Attributes
    // Had to make these static because of the initialization order problem.
    static bool mShowCursor;

    // Pure 3D attributes
    tPlatform* mpPlatform; 
    tContext* mpContext;

    // window properties.
    Resolution mResolution;
    int mbpp;
    bool mFullscreen;
    char mRenderer[ConfigString::MaxLength];
};

#endif // DCPLATFORM_H
