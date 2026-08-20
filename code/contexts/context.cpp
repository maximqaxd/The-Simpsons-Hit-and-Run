//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        context.cpp
//
// Description: Implementation of Context base class.
//
// History:     + Created -- Darwin Chau
//
//=============================================================================

//========================================
// System Includes
//========================================
#include <raddebug.hpp>

//========================================
// Project Includes
//========================================
#include <contexts/context.h>
#include <memory/srrmemory.h>


#include <debug/debuginfo.h>

// Managers.
//
#include <input/inputmanager.h>

#if defined RAD_DREAMCAST && defined RAD_DC_TRACE_BIG_ALLOCS
#include <memory/memoryutilities.h>
#endif

//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************

//******************************************************************************
//
// Public Member Functions
//
//******************************************************************************

//==============================================================================
// Context::DestroyInstance
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================
void Context::DestroyInstance()
{
    delete( GMA_PERSISTENT, this );
}

//==============================================================================
// Context::Start
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================
void Context::Start( ContextEnum previousContext )
{
#if defined RAD_DREAMCAST && defined RAD_DC_TRACE_BIG_ALLOCS
    rReleasePrintf( "[ctx] start from %d, heap %u KB\n", (int)previousContext, (unsigned)( Memory::GetTotalMemoryUsed() / 1024 ) );
#endif
    this->OnStart( previousContext );
}


//==============================================================================
// Context::Stop
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================
void Context::Stop( ContextEnum nextContext )
{
    this->OnStop( nextContext );
#if defined RAD_DREAMCAST && defined RAD_DC_TRACE_BIG_ALLOCS
    rReleasePrintf( "[ctx] stop to %d, heap %u KB\n", (int)nextContext, (unsigned)( Memory::GetTotalMemoryUsed() / 1024 ) );
#endif
}


//==============================================================================
// Context::Update
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================
void Context::Update( unsigned int elapsedTime )
{
    InputManager::GetInstance()->Update( elapsedTime );
    this->OnUpdate( elapsedTime );
}


//==============================================================================
// Context::HandleEvent
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================
void Context::HandleEvent( EventEnum id, void* pEventData )
{
    this->OnHandleEvent( id, pEventData );
}

//=============================================================================
// Context::Suspend
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void Context::Suspend()
{
    m_state = S_SUSPENDED;

    OnSuspend();
}

//=============================================================================
// Context::Resume
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void Context::Resume()
{
    m_state = S_ACTIVE;

    OnResume();
}

//******************************************************************************
//
// Protected Member Functions
//
//******************************************************************************


//==============================================================================
// Context::Context
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================// 
Context::Context()
{
}


//==============================================================================
// Context::~Context
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================// 
Context::~Context()
{
}

