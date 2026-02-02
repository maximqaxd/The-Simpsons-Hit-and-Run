//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File:        dccontroller.cpp
// Subsystem:  Foundation Technologies - Controller System (Dreamcast stub)
// Description: Stub implementation for Dreamcast. no actual input.
//              Exposes same radController API so the game links. Replace with
//              Maple/KOS input when ready.
//=============================================================================

#include "pch.hpp"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <radobject.hpp>
#include <radcontroller.hpp>
#include <raddebug.hpp>
#include <radstring.hpp>
#include <radobjectlist.hpp>
#include <radtime.hpp>
#include <radmemorymonitor.hpp>
#include "radcontrollerbuffer.hpp"

//============================================================================
// Internal Interfaces 
//============================================================================

struct IRadControllerInputPointDC
    : public IRadControllerInputPoint
{
    virtual void iInitialize( void ) = 0;
    virtual void iVirtualTimeReMapped( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeChanged( unsigned int virtualTime ) = 0;
};

struct IRadControllerDC
    : public IRadController
{
    virtual void iPoll( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeReMapped( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeChanged( unsigned int virtualTime ) = 0;
    virtual void iSetBufferTime( unsigned int milliseconds, unsigned int pollingRate ) = 0;
};

//============================================================================
// Globals
//============================================================================

static class radControllerSystemDC* s_pTheDCControllerSystem2 = NULL;
static radMemoryAllocator g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;

static const char* g_Dcipt[] = { "Button", "AnalogButton", "XAxis", "YAxis" };

struct DCInputPointDef { const char* m_pType; const char* m_pName; unsigned int m_Id; };
static DCInputPointDef g_DCPoints[] =
{
    { g_Dcipt[0], "DPadUp",        0 }, { g_Dcipt[0], "DPadDown",   1 },
    { g_Dcipt[0], "DPadLeft",      2 }, { g_Dcipt[0], "DPadRight",  3 },
    { g_Dcipt[0], "Start",         4 }, { g_Dcipt[0], "Back",       5 },
    { g_Dcipt[0], "A",             6 }, { g_Dcipt[0], "B",         7 },
    { g_Dcipt[0], "X",             8 }, { g_Dcipt[0], "Y",          9 },
    { g_Dcipt[0], "Black",        10 }, { g_Dcipt[0], "White",     11 },
    { g_Dcipt[1], "LeftTrigger",  12 }, { g_Dcipt[1], "RightTrigger", 13 },
    { g_Dcipt[2], "LeftStickX",   14 }, { g_Dcipt[3], "LeftStickY", 15 },
    { g_Dcipt[2], "RightStickX",  16 }, { g_Dcipt[3], "RightStickY", 17 }
};
static const unsigned int g_NumDCPoints = sizeof(g_DCPoints) / sizeof(g_DCPoints[0]);

//============================================================================
// radControllerOutputPointDC
//============================================================================

class radControllerOutputPointDC
    : public IRadControllerOutputPoint,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerOutputPointDC" )
    radControllerOutputPointDC( const char* pName ) : radRefCount(0), m_pName(pName), m_Gain(0.0f)
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerOutputPointDC" );
    }
    virtual const char* GetName( void ) { return m_pName; }
    virtual const char* GetType( void ) { return "Analog"; }
    virtual float GetGain( void ) { return m_Gain; }
    virtual void SetGain( float value )
    {
        m_Gain = ( value < 0.0f ) ? 0.0f : ( ( value > 1.0f ) ? 1.0f : value );
    }
    const char* m_pName;
    float m_Gain;
};

//============================================================================
// radControllerInputPointDC
//============================================================================

class radControllerInputPointDC
    : public IRadControllerInputPointDC,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerInputPointDC" )
    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        m_TimeInState = 0;
        m_TimeOfStateChange = virtualTime;
    }
    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
        (void)virtualTime;
        m_TimeInState++;
    }
    float CalculateNewValue( void ) { return 0.0f; }  // stub: no hardware
    virtual void iInitialize( void ) { m_Value = CalculateNewValue(); }
    virtual const char* GetName( void ) { return m_pName; }
    virtual const char* GetType( void ) { return m_pType; }
    virtual void SetTolerance( float percentage )
    {
        m_Tolerance = ( percentage < 0.0f ) ? 0.0f : ( ( percentage > 1.0f ) ? 1.0f : percentage );
    }
    virtual float GetTolerance( void ) { return m_Tolerance; }
    virtual void RegisterControllerInputPointCallback( IRadControllerInputPointCallback* pCallback, unsigned int userData = 0 )
    {
        rAssert( pCallback != NULL );
        ref< IRadWeakCallbackWrapper > xIWcr;
        radWeakCallbackWrapperCreate( &xIWcr, g_ControllerSystemAllocator );
        if ( xIWcr != NULL ) { xIWcr->SetWeakInterface( pCallback ); xIWcr->SetUserData( (void*)(uintptr_t)userData ); }
        m_xIOl_Callbacks->AddObject( xIWcr );
    }
    virtual void UnRegisterControllerInputPointCallback( IRadControllerInputPointCallback* pCallback )
    {
        rAssert( pCallback != NULL );
        m_xIOl_Callbacks->Reset();
        IRadWeakCallbackWrapper* pIWcr;
        if ( (pIWcr = reinterpret_cast<IRadWeakCallbackWrapper*>( m_xIOl_Callbacks->GetNext() )) )
        {
            if ( pIWcr->GetWeakInterface() == pCallback ) { m_xIOl_Callbacks->RemoveObject( pIWcr ); return; }
        }
        rAssertMsg( false, "Controller Input Point Callback Not Registered." );
    }
    virtual float GetCurrentValue( unsigned int* pTime = NULL )
    {
        if ( pTime != NULL ) *pTime = m_TimeInState;
        return ( ( m_MaxRange - m_MinRange ) * m_Value ) + m_MinRange;
    }
    virtual void SetRange( float min, float max ) { m_MinRange = min; m_MaxRange = max; }
    virtual void GetRange( float* pMin, float* pMax )
    {
        rAssert( pMin != NULL || pMax != NULL );
        if ( pMin != NULL ) *pMin = m_MinRange;
        if ( pMax != NULL ) *pMax = m_MaxRange;
    }
    radControllerInputPointDC( const char* pType, const char* pName, int id )
        : radRefCount(0), m_Value(0.0f), m_MinRange(0.0f), m_MaxRange(1.0f), m_Tolerance(0.0f),
          m_TimeInState(0), m_TimeOfStateChange(0), m_pType(pType), m_pName(pName), m_Identifier(id)
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerInputPointDC" );
        ::radObjectListCreate( &m_xIOl_Callbacks, g_ControllerSystemAllocator );
    }
    ~radControllerInputPointDC( void )
    {
        rAssertMsg( m_xIOl_Callbacks->GetSize() == 0, "Someone forgot to UnRegister an input point callback" );
    }
    float m_Value, m_MinRange, m_MaxRange, m_Tolerance;
    unsigned int m_TimeInState, m_TimeOfStateChange;
    const char* m_pType;
    const char* m_pName;
    int m_Identifier;
    ref< IRadObjectList > m_xIOl_Callbacks;
};

//============================================================================
// radControllerDC
//============================================================================

class radControllerDC
    : public IRadControllerDC,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerDC" )
    virtual void iPoll( unsigned int virtualTime ) { (void)virtualTime; }
    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        m_xIOl_InputPoints->Reset();
        IRadControllerInputPointDC* p;
        while ( (p = reinterpret_cast<IRadControllerInputPointDC*>( m_xIOl_InputPoints->GetNext() )) )
            p->iVirtualTimeReMapped( virtualTime );
    }
    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
        if ( GetRefCount() > 1 && m_xIOl_InputPoints != NULL )
        {
            m_xIOl_InputPoints->Reset();
            IRadControllerInputPointDC* p;
            while ( (p = reinterpret_cast<IRadControllerInputPointDC*>( m_xIOl_InputPoints->GetNext() )) )
                p->iVirtualTimeChanged( virtualTime );
        }
    }
    virtual void iSetBufferTime( unsigned int milliseconds, unsigned int pollingRate ) { (void)milliseconds; (void)pollingRate; }
    virtual bool IsConnected( void ) { return true; }
    virtual const char* GetType( void ) { return "DCStandard"; }
    virtual const char* GetClassification( void ) { return "Joystick"; }
    virtual unsigned int GetNumberOfInputPointsOfType( const char* pType )
    {
        unsigned int n = 0;
        m_xIOl_InputPoints->Reset();
        IRadControllerInputPoint* p;
        while ( (p = reinterpret_cast<IRadControllerInputPoint*>( m_xIOl_InputPoints->GetNext() )) )
            if ( strcmp( p->GetType(), pType ) == 0 ) n++;
        return n;
    }
    virtual unsigned int GetNumberOfOutputPointsOfType( const char* pType )
    {
        (void)pType;
        return m_xIOl_OutputPoints->GetSize();
    }
    virtual IRadControllerInputPoint* GetInputPointByTypeAndIndex( const char* pType, unsigned int indx )
    {
        unsigned int i = 0;
        m_xIOl_InputPoints->Reset();
        IRadControllerInputPoint* p;
        while ( (p = reinterpret_cast<IRadControllerInputPoint*>( m_xIOl_InputPoints->GetNext() )) )
        {
            if ( strcmp( p->GetType(), pType ) == 0 && i++ == indx ) return p;
        }
        return NULL;
    }
    virtual IRadControllerOutputPoint* GetOutputPointByTypeAndIndex( const char* pType, unsigned int indx )
    {
        (void)pType;
        return reinterpret_cast<IRadControllerOutputPoint*>( m_xIOl_OutputPoints->GetAt( indx ) );
    }
    virtual IRadControllerInputPoint* GetInputPointByName( const char* pName )
    {
        m_xIOl_InputPoints->Reset();
        IRadControllerInputPoint* p;
        while ( (p = reinterpret_cast<IRadControllerInputPoint*>( m_xIOl_InputPoints->GetNext() )) )
        {
            if ( strcmp( p->GetName(), pName ) == 0 ) return p;
        }
        return NULL;
    }
    virtual IRadControllerOutputPoint* GetOutputPointByName( const char* pName )
    {
        for ( unsigned int i = 0; i < m_xIOl_OutputPoints->GetSize(); i++ )
        {
            IRadControllerOutputPoint* p = reinterpret_cast<IRadControllerOutputPoint*>( m_xIOl_OutputPoints->GetAt( i ) );
            if ( p && strcmp( p->GetName(), pName ) == 0 ) return p;
        }
        return NULL;
    }
    virtual unsigned int GetNumberOfInputPoints( void ) { return m_xIOl_InputPoints->GetSize(); }
    virtual IRadControllerInputPoint* GetInputPointByIndex( unsigned int index )
    {
        return reinterpret_cast<IRadControllerInputPoint*>( m_xIOl_InputPoints->GetAt( index ) );
    }
    virtual unsigned int GetNumberOfOutputPoints( void ) { return m_xIOl_OutputPoints->GetSize(); }
    virtual IRadControllerOutputPoint* GetOutputPointByIndex( unsigned int index )
    {
        return reinterpret_cast<IRadControllerOutputPoint*>( m_xIOl_OutputPoints->GetAt( index ) );
    }
    virtual const char* GetLocation( void ) { return m_xIString_Location->GetChars(); }
    radControllerDC( unsigned int thisAllocator, unsigned int virtualTime, unsigned int bufferTime, unsigned int pollingRate )
        : radRefCount(0)
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerDC" );
        (void)thisAllocator; (void)bufferTime; (void)pollingRate;
        ::radObjectListCreate( &m_xIOl_InputPoints, g_ControllerSystemAllocator );
        ::radObjectListCreate( &m_xIOl_OutputPoints, g_ControllerSystemAllocator );
        ::radStringCreate( &m_xIString_Location, g_ControllerSystemAllocator );
        m_xIString_Location->SetSize( 12 );
        m_xIString_Location->Append( "Port" );
        m_xIString_Location->Append( (unsigned int)0 );
        m_xIString_Location->Append( "\\Slot0" );
        for ( unsigned int i = 0; i < g_NumDCPoints; i++ )
        {
            ref< radControllerInputPointDC > pInputPoint = new( g_ControllerSystemAllocator ) radControllerInputPointDC
                ( g_DCPoints[i].m_pType, g_DCPoints[i].m_pName, (int)g_DCPoints[i].m_Id );
            m_xIOl_InputPoints->AddObject( pInputPoint );
            pInputPoint->iInitialize();
        }
        radControllerOutputPointDC* pLeft  = new( g_ControllerSystemAllocator ) radControllerOutputPointDC( "LeftMotor" );
        radControllerOutputPointDC* pRight = new( g_ControllerSystemAllocator ) radControllerOutputPointDC( "RightMotor" );
        m_xIOl_OutputPoints->AddObject( reinterpret_cast<IRefCount*>( pLeft ) );
        m_xIOl_OutputPoints->AddObject( reinterpret_cast<IRefCount*>( pRight ) );
        iSetBufferTime( bufferTime, pollingRate );
        iVirtualTimeReMapped( virtualTime );
    }
    ~radControllerDC( void ) {}
    ref< IRadObjectList > m_xIOl_InputPoints;
    ref< IRadObjectList > m_xIOl_OutputPoints;
    ref< IRadString >    m_xIString_Location;
};

//============================================================================
// radControllerSystemDC
//============================================================================

class radControllerSystemDC
    : public IRadControllerSystem,
    public IRadTimerCallback,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerSystemDC" )
    virtual void OnTimerDone( unsigned int elapsedtime, void* pUserData )
    {
        (void)elapsedtime; (void)pUserData;
        m_xIOl_Controllers->Reset();
        IRadControllerDC* p;
        while ( (p = reinterpret_cast<IRadControllerDC*>( m_xIOl_Controllers->GetNext() )) )
            p->iPoll( radTimeGetMilliseconds() + m_VirtualTimeAdjust );
        if ( !m_UsingVirtualTime ) SetVirtualTime( radTimeGetMilliseconds() );
    }
    virtual unsigned int GetNumberOfControllers( void ) { return m_xIOl_Controllers->GetSize(); }
    virtual IRadController* GetControllerByIndex( unsigned int myindex )
    {
        return reinterpret_cast<IRadController*>( m_xIOl_Controllers->GetAt( myindex ) );
    }
    virtual IRadController* GetControllerAtLocation( const char* pLocation )
    {
        rAssert( pLocation != NULL );
        m_xIOl_Controllers->Reset();
        IRadController* pIC2;
        while ( (pIC2 = reinterpret_cast<IRadController*>( m_xIOl_Controllers->GetNext() )) )
        {
            if ( strcmp( pIC2->GetLocation(), pLocation ) == 0 ) return pIC2;
        }
        return NULL;
    }
    virtual void SetBufferTime( unsigned int milliseconds )
    {
        if ( milliseconds == 0 ) { m_UsingVirtualTime = false; MapVirtualTime( 0, 0 ); milliseconds = 10; }
        else m_UsingVirtualTime = true;
        unsigned int pollingRate = m_xITimer ? m_xITimer->GetTimeout() : 10;
        m_EventBufferTime = milliseconds;
        m_xIOl_Controllers->Reset();
        IRadControllerDC* p;
        while ( (p = reinterpret_cast<IRadControllerDC*>( m_xIOl_Controllers->GetNext() )) )
            p->iSetBufferTime( milliseconds, pollingRate );
    }
    virtual void MapVirtualTime( unsigned int systemTicks, unsigned int virtualTicks )
    {
        m_VirtualTimeAdjust = (int)( (int)virtualTicks - (int)systemTicks );
        m_xIOl_Controllers->Reset();
        IRadControllerDC* p;
        while ( (p = reinterpret_cast<IRadControllerDC*>( m_xIOl_Controllers->GetNext() )) )
            p->iVirtualTimeReMapped( radTimeGetMilliseconds() + m_VirtualTimeAdjust );
    }
    virtual void SetVirtualTime( unsigned int virtualTime )
    {
        m_xIOl_Controllers->Reset();
        IRadControllerDC* p;
        while ( (p = reinterpret_cast<IRadControllerDC*>( m_xIOl_Controllers->GetNext() )) )
            p->iVirtualTimeChanged( virtualTime );
    }
    virtual void SetCaptureRate( unsigned int ms )
    {
        if ( m_xITimer ) m_xITimer->SetTimeout( ms );
        SetBufferTime( m_EventBufferTime );
    }
    virtual void RegisterConnectionChangeCallback( IRadControllerConnectionChangeCallback* pCallback )
    {
        rAssert( pCallback != NULL );
        ref< IRadWeakInterfaceWrapper > xIWir;
        ::radWeakInterfaceWrapperCreate( &xIWir, g_ControllerSystemAllocator );
        xIWir->SetWeakInterface( pCallback );
        m_xIOl_Callbacks->AddObject( xIWir );
    }
    virtual void UnRegisterConnectionChangeCallback( IRadControllerConnectionChangeCallback* pCallback )
    {
        rAssert( pCallback != NULL );
        m_xIOl_Callbacks->Reset();
        IRadWeakInterfaceWrapper* pIWir;
        while ( (pIWir = reinterpret_cast<IRadWeakInterfaceWrapper*>( m_xIOl_Callbacks->GetNext() )) )
        {
            if ( pIWir->GetWeakInterface() == pCallback ) { m_xIOl_Callbacks->RemoveObject( pIWir ); return; }
        }
        rAssertMsg( false, "Controller connection change callback not registered." );
    }
    void Service( void ) { if ( m_xITimerList != NULL ) m_xITimerList->Service(); }
    radControllerSystemDC( IRadControllerConnectionChangeCallback* pConnectionChangeCallback, radMemoryAllocator thisAllocator )
        : m_UsingVirtualTime(false), m_VirtualTimeAdjust(0), m_EventBufferTime(0), m_DefaultConnectionChangeCallback(NULL)
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerSystemDC" );
        rAssert( s_pTheDCControllerSystem2 == NULL );
        s_pTheDCControllerSystem2 = this;
        g_ControllerSystemAllocator = thisAllocator;
        radTimeCreateList( &m_xITimerList, 1, g_ControllerSystemAllocator );
        m_xITimerList->CreateTimer( &m_xITimer, 10, this );
        ::radObjectListCreate( &m_xIOl_Controllers, g_ControllerSystemAllocator );
        ::radObjectListCreate( &m_xIOl_Callbacks, g_ControllerSystemAllocator );
        m_DefaultConnectionChangeCallback = pConnectionChangeCallback;
        if ( pConnectionChangeCallback ) RegisterConnectionChangeCallback( pConnectionChangeCallback );
        // One stub controller so game sees a controller (all inputs 0)
        ref< IRadController > xIController2 = new ( g_ControllerSystemAllocator ) radControllerDC
            ( g_ControllerSystemAllocator, radTimeGetMilliseconds() + m_VirtualTimeAdjust, m_EventBufferTime, 10 );
        m_xIOl_Controllers->AddObject( xIController2 );
        m_xIOl_Callbacks->Reset();
        IRadWeakInterfaceWrapper* pIWir;
        while ( (pIWir = reinterpret_cast<IRadWeakInterfaceWrapper*>( m_xIOl_Callbacks->GetNext() )) )
        {
            IRadControllerConnectionChangeCallback* pCallback = (IRadControllerConnectionChangeCallback*)pIWir->GetWeakInterface();
            pCallback->OnControllerConnectionStatusChange( xIController2 );
        }
        SetCaptureRate( 10 );
        MapVirtualTime( 0, 0 );
        SetBufferTime( 0 );
    }
    ~radControllerSystemDC( void )
    {
        if ( m_DefaultConnectionChangeCallback != NULL )
        {
            UnRegisterConnectionChangeCallback( m_DefaultConnectionChangeCallback );
            m_DefaultConnectionChangeCallback = NULL;
        }
        rAssertMsg( m_xIOl_Callbacks->GetSize() == 0, "Somebody forgot to unregister a controller connection change callback" );
        g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;
        rAssert( s_pTheDCControllerSystem2 == this );
        s_pTheDCControllerSystem2 = NULL;
    }
    bool m_UsingVirtualTime;
    int m_VirtualTimeAdjust;
    unsigned int m_EventBufferTime;
    IRadControllerConnectionChangeCallback* m_DefaultConnectionChangeCallback;
    ref< IRadObjectList > m_xIOl_Callbacks;
    ref< IRadObjectList > m_xIOl_Controllers;
    ref< IRadTimer >     m_xITimer;
    ref< IRadTimerList > m_xITimerList;
};

//============================================================================
// radControllerInitialize / Terminate / Get / Service
//============================================================================

void radControllerInitialize( IRadControllerConnectionChangeCallback* pConnectionChangeCallback, radMemoryAllocator alloc )
{
    rAssert( s_pTheDCControllerSystem2 == NULL );
    new ( alloc ) radControllerSystemDC( pConnectionChangeCallback, alloc );
}

void radControllerTerminate( void )
{
    radRelease( s_pTheDCControllerSystem2, NULL );
}

IRadControllerSystem* radControllerSystemGet( void )
{
    rAssert( s_pTheDCControllerSystem2 != NULL );
    return s_pTheDCControllerSystem2;
}

void radControllerSystemService( void )
{
    if ( s_pTheDCControllerSystem2 != NULL )
        s_pTheDCControllerSystem2->Service();
}
