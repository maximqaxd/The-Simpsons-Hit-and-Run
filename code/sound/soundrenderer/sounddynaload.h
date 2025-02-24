//=============================================================================
// Copyright (C) 2001 Radical Entertainment Ltd.  All rights reserved.
//
// File:        sounddynaload.hpp
//
// Subsystem:   Dark Angel - Dynamic Loading System
//
// Description: Description of the DA Dynamic Sound Loading System
//
// Revisions:
//  + Created: Novemeber 22, 2001 -- breimer
//
//=============================================================================

#ifndef _SOUNDDYNALOAD_HPP
#define _SOUNDDYNALOAD_HPP

//=============================================================================
// Included Files
//=============================================================================

#include <radobject.hpp>
#include <radlinkedclass.hpp>

#ifndef RAD_DREAMCAST
#include <radsound.hpp>
#endif

//=============================================================================
// Namespace
//=============================================================================


namespace Sound {

//=============================================================================
// Prototypes
//=============================================================================

class daSoundDynaLoadRegion;
class daSoundDynaLoadManager;
class daSoundFileInstance;

//=============================================================================
// Class Declarations
//=============================================================================

struct IDaSoundDynaLoadCompletionCallback :  public IRefCount
{
    virtual void OnDynaLoadOperationsComplete( void* pUserData ) = 0;
};

//
// A dynamic load region
//
class daSoundDynaLoadRegion :  public radLinkedClass< daSoundDynaLoadRegion >,
                               public radRefCount
{
public:

    enum SlotState {
        Empty,
        Initializing,
        Ready,
        Destroying
    };
    
    IMPLEMENT_REFCOUNTED( "daSoundDynaLoadRegion" );
    daSoundDynaLoadRegion( );
    virtual ~daSoundDynaLoadRegion( );

    // Create and destroy the region
    void Create
    (
#ifndef RAD_DREAMCAST
        IRadSoundHalMemoryRegion* pMemRegion,
#endif
        unsigned int sizeofslots,
        unsigned int numslots
    );
    void Destroy( void );
    void ServiceOncePerFrame( void );

    // Track if there are any pending swaps registered.
    static unsigned int GetNumPendingSwaps( void );

    //
    // daSoundDynaLoadRegion
    //
    void SwapInObject
    (
        unsigned int slot,
        daSoundFileInstance* pObject
    );
    daSoundDynaLoadRegion::SlotState GetSlotState
    (
        unsigned int slot
    );
    unsigned int GetNumSlots( void );
    unsigned int GetSlotSize( void );

protected:
    bool SharedMemoryRegions( void );

    void PerformSwap( unsigned int slot );
#ifndef RAD_DREAMCAST
    IRadSoundHalMemoryRegion* GetSlotMemoryRegion( unsigned int slot );
#endif
    void SetSlotObject( unsigned int slot, daSoundFileInstance* pObject );
    daSoundFileInstance* GetSlotObject( unsigned int slot );

    void SetPendingSwapObject( unsigned int slot, daSoundFileInstance * pObject );
    bool ArePendingSwapsRegistered( void );
    daSoundFileInstance* GetPendingSwapObject( unsigned int slot );

    void SetObject_Internal
    (
        daSoundFileInstance** ppObjects,
        unsigned int slot,
        daSoundFileInstance* pObject
    );
    daSoundFileInstance* GetObject_Internal
    (
        daSoundFileInstance** ppObjects,
        unsigned int slot
    );

    void ClearActiveSwap( void );
    bool SetActiveSwap( unsigned int slot );

private:
    bool                                m_IsInitialized;

    // The active swap
    static daSoundDynaLoadRegion*       s_pActiveRegion;
    static unsigned int                 s_ActiveSlot;

    // Region information
    unsigned int                        m_NumSlots;
    unsigned int                        m_SlotSize;

    // Allocated regions
#ifndef RAD_DREAMCAST
    IRadSoundHalMemoryRegion**          m_ppSlot;
#endif
    daSoundFileInstance**            m_ppSlotObjects;
    daSoundFileInstance**            m_ppPendingSwapObjects;

    // Count of pending swap operations
    unsigned int                        m_PendingSwapCount;
    static unsigned int                 s_GlobalPendingSwapCount;
};

//
// Dynamic loading system interface
//
class daSoundDynaLoadManager : public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "daSoundDynaLoadManager" );
    daSoundDynaLoadManager( );
    virtual ~daSoundDynaLoadManager( );

    static daSoundDynaLoadManager* GetInstance( void );

    //
    // daSoundDynaLoadRegion
    //
    virtual void ServiceOncePerFrame( void );

    daSoundDynaLoadRegion* CreateRegion
    (
#ifndef RAD_DREAMCAST
        IRadSoundHalMemoryRegion* pMemRegion,
#endif
        unsigned int sizeofslots,
        unsigned int numslots
    );
    daSoundDynaLoadRegion* CreateRegionFromTotalSpace
    (
#ifndef RAD_DREAMCAST
        IRadSoundHalMemoryRegion* pMemRegion,
#endif
        unsigned int sizeofslots
    );
    void AddCompletionCallback
    (
        IDaSoundDynaLoadCompletionCallback* pCallback,
        void* pUserData
    );
    unsigned int GetNumPendingSwaps( void );

protected:
private:
    //
    // Store this class as a singleton
    //
    static daSoundDynaLoadManager*          s_pSingleton;

    //
    // Store a completion callback
    //
    IDaSoundDynaLoadCompletionCallback*     m_pCompletionCallback;
    void*                                   m_pCompletionUserData;
};

} // Sound Namespace
#endif //_SOUNDDYNALOAD_HPP

