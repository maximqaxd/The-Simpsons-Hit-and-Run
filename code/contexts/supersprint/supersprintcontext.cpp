//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        supersprintcontext.cpp
//
// Description: Implement SuperSprintContext
//
// History:     2/8/2003 + Created -- Cary Brisebois
//
//=============================================================================

//========================================
// System Includes
//========================================
// Foundation Tech
#include <raddebug.hpp>

#include <p3d/shadow.hpp>


//========================================
// Project Includes
//========================================

#include <ai/vehicle/vehicleairender.h>

#include <contexts/supersprint/supersprintcontext.h>

#include <memory/srrmemory.h>

#include <gameflow/gameflow.h>
#include <main/commandlineoptions.h>
#include <main/game.h>
#include <memory/leakdetection.h>
#include <render/RenderManager/RenderManager.h>
#include <render/RenderManager/RenderLayer.h>

#include <worldsim/coins/coinmanager.h>
#include <worldsim/coins/sparkle.h>
#include <worldsim/hitnrunmanager.h>
#include <worldsim/vehiclecentral.h>
#include <worldsim/worldphysicsmanager.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/traffic/trafficmanager.h>
#include <worldsim/ped/pedestrianmanager.h>
#include <worldsim/skidmarks/skidmarkmanager.h>

#include <render/breakables/breakablesmanager.h>
#include <render/Particles/particlemanager.h>
#include <render/animentitydsgmanager/animentitydsgmanager.h>
#include <render/RenderManager/WorldRenderLayer.h>
#include <worldsim/skidmarks/SkidMarkGenerator.h>
#include <sound/soundmanager.h>
#include <meta/triggervolumetracker.h>

#include <pedpaths/pathmanager.h>

#include <input/inputmanager.h>

// TODO: Remove once we put CreateRoadNetwork in the levels pipe
#include <roads/roadmanager.h>
#include <p3d/light.hpp>
#include <p3d/view.hpp>

#include <worldsim/worldobject.h>
#include <ai/actionbuttonmanager.h>

#include <camera/supercammanager.h>
#include <camera/supercamcentral.h>
#include <camera/supercam.h>
#include <camera/supersprintcam.h>
#include <camera/bumpercam.h>
#include <camera/kullcam.h>
#include <camera/wrecklesscam.h>
#include <camera/followcam.h>
#include <camera/animatedcam.h>
#include <camera/walkercam.h>

#include <supersprint/supersprintmanager.h>

#include <ai/actor/actormanager.h>
#include <presentation/presentation.h>

#include <mission/animatedicon.h>

#include <presentation/gui/guisystem.h>
#include <presentation/gui/guiwindow.h>

#include <render/animentitydsgmanager/animentitydsgmanager.h>

//*****************************************************************************
//
// Global Data, Local Data, Local Classes
//
//*****************************************************************************

SuperSprintContext* SuperSprintContext::spInstance = NULL;
const int NUM_SKID_MARKS_SUPER_SPRINT = 30;

//*****************************************************************************
//
// Public Member Functions
//
//*****************************************************************************

SuperSprintContext* SuperSprintContext::GetInstance()
{
    if ( spInstance == NULL )
    {
        HeapMgr()->PushHeap( GMA_PERSISTENT );
        spInstance = new SuperSprintContext();
        HeapMgr()->PopHeap(GMA_PERSISTENT);
    }

    return spInstance;
}

//*****************************************************************************
//
// Protected Member Functions
//
//*****************************************************************************

//=============================================================================
// SuperSprintContext::OnStart
//=============================================================================
// Description: Comment
//
// Parameters:  ( ContextEnum previousContext )
//
// Return:      void 
//
//=============================================================================
void SuperSprintContext::OnStart( ContextEnum previousContext )
{
    this->Resume();

    // Common to all playing contexts.
    //
    PlayingContext::OnStart( previousContext );

    ////////////////////////////////////////////////////////////
    // RenderManager 
    RenderManager* rm = GetRenderManager();
    RenderLayer* rl = rm->mpLayer( RenderEnums::LevelSlot );
    rAssert( rl );

    GetRenderManager()->mpLayer( RenderEnums::PresentationSlot )->Chill();

#ifdef DEBUGWATCH
    // bootstrap vehicleai renderer
    VehicleAIRender::GetVehicleAIRender();
#endif

    ////////////////////////////////////////////////////////////
    // Cameras set up
    rl->SetNumViews( 1 );
    rl->SetUpViewCam();

    p3d::inventory->SelectSection("Default");
    tLightGroup* sun = p3d::find<tLightGroup>("sun");   
    rAssert( sun );
    for( unsigned int i = 0; i < rl->GetNumViews(); i++ )
    {
        for(int j=0;j<sun->GetNumLights();j++)
        {
            rl->pView(i)->AddLight( sun->GetLight(j) );
        }
    }  
    // Disable mood lighting in supersprint mode
    rm->SetLevelLayerLights( NULL );


    float aspect = p3d::display->IsWidescreen() ? (16.0f / 9.0f) : (4.0f / 3.0f);

    unsigned int view = 0;

    tPointCamera* cam = static_cast<tPointCamera*>( rl->pCam( view ) );
    rAssert( dynamic_cast<tPointCamera*> ( cam ) != NULL );
    rAssert( cam );

    SuperCam* sc = NULL;
    sc = new SuperSprintCam();
    sc->SetAspect( aspect );

    for( int i = 0; i < GetGameplayManager()->GetNumPlayers(); ++i )
    {
        SuperCamCentral* scc = GetSuperCamManager()->GetSCC( i );
        rAssert( scc );

        scc->SetCamera( cam );
        scc->RegisterSuperCam( sc );

        //Start with the right cam
        scc->SelectSuperCam( SuperCam::SUPER_SPRINT_CAM, SuperCamCentral::CUT | SuperCamCentral::QUICK, 0 );
    }

    //Hack to add the other cameras to player 0
    SuperCamCentral* scc = GetSuperCamManager()->GetSCC( 0 );
    sc = new BumperCam();
    sc->SetAspect( aspect );
    scc->RegisterSuperCam( sc );

    FollowCam* fc = new FollowCam( FollowCam::FOLLOW_NEAR );
    fc->SetAspect( aspect );
    fc->CopyToData();
    scc->RegisterSuperCam( fc );

    fc = new FollowCam( FollowCam::FOLLOW_FAR );
    fc->SetAspect( aspect );
    fc->CopyToData();
    scc->RegisterSuperCam( fc );

    sc = new KullCam();
    sc->SetAspect( aspect );
    scc->RegisterSuperCam( sc );

    sc = new WrecklessCam();
    sc->SetAspect( aspect );
    scc->RegisterSuperCam( sc );

    sc = new AnimatedCam();
    sc->SetAspect( aspect );
    scc->RegisterSuperCam( sc );

    sc = new ComedyCam();
    sc->SetAspect( aspect );
    scc->RegisterSuperCam( sc );

    ////////////////////////////////////////////////////////////
    // AvatarManager Bootstrapping
    GetAvatarManager( )->EnterGame( );

    ////////////////////////////////////////////////////////////
    // TrafficManager Init
    //TrafficManager::GetInstance()->Init();

    ////////////////////////////////////////////////////////////
    // PedestrianManager Init
    PedestrianManager::GetInstance()->Init();

    // TODO: Move this into level pipe
    //Set up the sorting of the intersections and stuff.
    RoadManager::GetInstance()->CreateRoadNetwork();

    ////////////////////////////////////////////////////////////
    // SkidMark Init Init
    //Find skid mark shaders in the inventory and set proper values
    SkidMarkGenerator::InitShaders();
    GetSkidmarkManager()->Init( NUM_SKID_MARKS_SUPER_SPRINT );
    GetSkidmarkManager()->SetTimedFadeouts( true );


    ////////////////////////////////////////////////////////////
    // OnStart calls

    rm->pWorldScene()->SetRenderAll(true);

    GetSSM()->StartRace();

    //
    // Notify the sound system of gameplay start.  This has been moved after
    // the StartRace call above, since that causes character position in or out 
    // of the car to be decided, which the sound system uses to determine 
    // which sounds to start playing.
    //
#ifndef RAD_DREAMCAST
        SoundManager::GetInstance()->OnGameplayStart();
#endif

    GetGuiSystem()->HandleMessage( GUI_MSG_RUN_MINIGAME,
                                   CGuiWindow::GUI_SCREEN_ID_MINI_HUD );

    // register GUI user input handler for active players only
    //
    int activeControllerIDs = 0;
    for( int i = 0; i < SuperSprintData::NUM_PLAYERS; i++ )
    {
        int controllerID = GetInputManager()->GetControllerIDforPlayer( i );
        if( controllerID != -1 )
        {
            activeControllerIDs |= (1 << controllerID);
        }
    }

    GetGuiSystem()->RegisterUserInputHandlers( activeControllerIDs );

    GetInputManager()->SetGameState( Input::ACTIVE_SS_GAME );
}

//=============================================================================
// SuperSprintContext::OnStop
//=============================================================================
// Description: Comment
//
// Parameters:  ( ContextEnum nextContext )
//
// Return:      void 
//
//=============================================================================
void SuperSprintContext::OnStop( ContextEnum nextContext )
{
    GetInputManager()->EnableReset( false );

    GetInputManager()->SetGameState( Input::ACTIVE_NONE );

    // unregister GUI user input handlers
    //
    int activeControllerIDs = 0;
    for( int i = 0; i < SuperSprintData::NUM_PLAYERS; i++ )
    {
        int controllerID = GetInputManager()->GetControllerIDforPlayer( i );
        if( controllerID != -1 )
        {
            activeControllerIDs |= (1 << controllerID);
        }
    }

    GetGuiSystem()->UnregisterUserInputHandlers( activeControllerIDs );

    HeapMgr()->PushHeap (GMA_TEMP);

    //
    // This is called to prevent DMA of destroyed textures,
    // because we don't swap buffers until the next frame.
    //
    p3d::pddi->DrawSync();

    const bool shutdown = true;
    GetSuperCamManager()->Init( shutdown );

    TriggerVolumeTracker::GetInstance()->Cleanup();

    GetCoinManager()->Destroy();
    GetSparkleManager()->Destroy();
	GetHitnRunManager()->Destroy();

    GetPresentationManager()->Finalize();

    //Shut down the SuperSprintManager
    GetGameplayManager()->Finalize();

    GetSkidmarkManager()->Destroy();

    GetRenderManager()->mpLayer( RenderEnums::PresentationSlot )->Warm();

    // Clean up lights!
    //
    RenderLayer* rl = GetRenderManager()->mpLayer( RenderEnums::LevelSlot );
    rAssert( rl );
    for( unsigned int i = 0; i < rl->GetNumViews(); i++ )
    {
        rl->pView(i)->RemoveAllLights ();
    }  

#ifdef DEBUGWATCH
    VehicleAIRender::Destroy();
#endif

    GetPresentationManager()->OnGameplayStop();

    //Destroy Avatars stuff first
    GetAvatarManager()->Destroy();
    TrafficManager::DestroyInstance();
    PedestrianManager::DestroyInstance();
    GetCharacterManager()->Destroy();
    GetVehicleCentral()->Unload();
    GetActorManager()->RemoveAllActors();
    GetActorManager()->RemoveAllSpawnPoints();

    //We never entered.
    GetActionButtonManager( )->Destroy( );

    GetBreakablesManager()->FreeAllBreakables();
    GetParticleManager()->ClearSystems();
    GetRenderManager()->DumpAllLoadedData();
    SkidMarkGenerator::ReleaseShaders();

#ifndef RAD_DREAMCAST
    GetSoundManager()->OnGameplayEnd( true );
#endif
    PathManager::GetInstance()->Destroy();

    //This does cleanup.
    RoadManager::GetInstance()->Destroy();

    // Cleanup the Avatar Manager
    //
    GetAvatarManager()->ExitGame();

    // Flush out the special section used by physics to cache SkeletonInfos.
    //
    p3d::inventory->RemoveSectionElements (SKELCACHE);
    p3d::inventory->DeleteSection (SKELCACHE);
    rReleaseString ("Delete SKELCACHE inventory section\n");

    // enable screen clearing
    //
    GetRenderManager()->mpLayer( RenderEnums::GUI )->pView( 0 )->SetClearMask( PDDI_BUFFER_ALL );

#ifndef RAD_RELEASE
    // Dump out the contents of the inventory sections
    //
    p3d::inventory->Dump (true);
#endif

    AnimatedIcon::ShutdownAnimatedIcons();
    GetAnimEntityDSGManager()->RemoveAll();

    HeapMgr()->PopHeap (GMA_TEMP);

    // Common to all playing contexts.
    //
    PlayingContext::OnStop( nextContext );
}

//=============================================================================
// SuperSprintContext::OnUpdate
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int elapsedTime )
//
// Return:      void 
//
//=============================================================================
void SuperSprintContext::OnUpdate( unsigned int elapsedTime )
{
    if( m_state != S_SUSPENDED )
    {
        float timeins = (float)(elapsedTime) / 1000.0f;
        GetAvatarManager()->Update( timeins );

        GetCharacterManager()->GarbageCollect( );

        ///////////////////////////////////////////////////////////////
        // Update Particles
        GetParticleManager()->Update( elapsedTime );
        GetBreakablesManager()->Update( elapsedTime );
        GetAnimEntityDSGManager()->Update( elapsedTime );

        ///////////////////////////////////////////////////////////////
        // Update Gameplay Manager
        GetGameplayManager()->Update( elapsedTime );

        ///////////////////////////////////////////////////////////////
        // Update Physics
        GetWorldPhysicsManager()->Update(elapsedTime);

        ///////////////////////////////////////////////////////////////
        // Update Peds
        // ordering is important. Unless other parts of code change, we must call
        // this before WorldPhysManager::Update() because PedestrianManager
        // sets the flags for all characters to be updated in WorldPhys Update 
        PedestrianManager::GetInstance()->Update( elapsedTime );

        ///////////////////////////////////////////////////////////////
        // Update Trigger volumes
        GetTriggerVolumeTracker()->Update( elapsedTime );

        ///////////////////////////////////////////////////////////////
        // Update Traffic
        //TrafficManager::GetInstance()->Update( elapsedTime );

        ///////////////////////////////////////////////////////////////
        // Update Skidmarks (fading)
        GetSkidmarkManager()->Update( elapsedTime );
        GetSparkleManager()->Update( elapsedTime );

        //Update the Super Sprint Manager
        GetSSM()->Update( elapsedTime );
    }

    // Common to all playing contexts.
    //
    PlayingContext::OnUpdate( elapsedTime );
}

//=============================================================================
// SuperSprintContext::OnSuspend
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void SuperSprintContext::OnSuspend()
{
    // Common to all playing contexts.
    //
    PlayingContext::OnSuspend();
}

//=============================================================================
// SuperSprintContext::OnResume
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void SuperSprintContext::OnResume()
{
    // Common to all playing contexts.
    //
    PlayingContext::OnResume();
}

//*****************************************************************************
//
// Private Member Functions
//
//*****************************************************************************

//=============================================================================
// SuperSprintContext::SuperSprintContext
//=============================================================================
// Description: Constructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//=============================================================================
SuperSprintContext::SuperSprintContext()
{
}

//=============================================================================
// SuperSprintContext::~SuperSprintContext
//=============================================================================
// Description: Destructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//=============================================================================
SuperSprintContext::~SuperSprintContext()
{
}

