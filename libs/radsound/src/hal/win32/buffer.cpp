//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//============================================================================
// Include Files
//============================================================================

#include "pch.hpp"
#include <stdio.h>
#include "buffer.hpp"
#include "bufferloader.hpp"
#include "system.hpp"

const unsigned int RADSOUNDHAL_BUFFER_CHANNEL_ALIGNMENT = 1;

//============================================================================
// Static Initialization
//============================================================================

radSoundHalBufferWin * radSoundHalBufferWin::s_pLinkedClassHead = NULL;
radSoundHalBufferWin * radSoundHalBufferWin::s_pLinkedClassTail = NULL;
ref< IRadMemoryPool > radSoundHalBufferWin::s_refIRadMemoryPool = NULL;
radSoundHalBufferWin::BufferData * radSoundHalBufferWin::s_pLRUFreeBufferListHead = NULL;
radSoundHalBufferWin::BufferData * radSoundHalBufferWin::s_pLRUFreeBufferListTail = NULL;
unsigned int radSoundHalBufferWin::DebugInfo::s_TotalFreeCount = 0;
unsigned int radSoundHalBufferWin::DebugInfo::s_TotalBusyCount = 0;
unsigned int radSoundHalBufferWin::DebugInfo::s_Total = 0;

//========================================================================
// radSoundHalBufferWin::~radSoundHalBufferWin
//========================================================================

radSoundHalBufferWin::~radSoundHalBufferWin( void )
{
    BufferData * pBufferDataIter = NULL;

    pBufferDataIter = m_pFreeBufferDataList_Pos;

    while( pBufferDataIter != NULL )
    {
        BufferData * pOldData = pBufferDataIter;
        pBufferDataIter = pBufferDataIter->m_pNext;
        RemoveFromList( pOldData );
        DeleteBufferData( pOldData );
    }

    pBufferDataIter = m_pFreeBufferDataList_NonPos;

    while( pBufferDataIter != NULL )
    {
        BufferData * pOldData = pBufferDataIter;
        pBufferDataIter = pBufferDataIter->m_pNext;
        RemoveFromList( pOldData );
        DeleteBufferData( pOldData );
    }

    pBufferDataIter = m_pBusyBufferDataList;

    while( pBufferDataIter != NULL )
    {
        BufferData * pOldData = pBufferDataIter;
        pBufferDataIter = pBufferDataIter->m_pNext;
        RemoveFromList( pOldData );
        DeleteBufferData( pOldData );
    }
}

//========================================================================
// radSoundHalBufferWin::radSoundHalBufferWin
//========================================================================

radSoundHalBufferWin::radSoundHalBufferWin(	void )
    :
    m_SizeInFrames( 0 ),
    m_LoadStartInBytes( 0 ),
    m_pLockedLoadBuffer( NULL ),
    m_LockedLoadBytes( 0 ),
    m_Looping( false ),
    m_Streaming( false ),
    m_pFreeBufferDataList_Pos( NULL ),
    m_pFreeBufferDataList_NonPos( NULL ),
    m_pBusyBufferDataList( NULL ),
    m_refIRadSoundHalAudioFormat( NULL ),
    m_refIRadMemoryObject( NULL ),
    m_refIRadSoundHalBufferLoadCallback( NULL )
{
    ::memset( & m_DebugInfo, 0, sizeof( m_DebugInfo ) );
}

//========================================================================
// radSoundHalBufferWin::Initialize
//========================================================================

void radSoundHalBufferWin::Initialize
(
	IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
    IRadMemoryObject * pIRadMemoryObject,
    unsigned int sizeInFrames,
	bool looping,
    bool streaming
)
{
    rAssert( pIRadSoundHalAudioFormat != NULL );
    rAssert( pIRadSoundHalAudioFormat->GetEncoding() == IRadSoundHalAudioFormat::PCM );
	rAssert( pIRadMemoryObject->GetMemorySize( ) >= ::radSoundHalBufferCalculateMemorySize( 
        IRadSoundHalAudioFormat::Bytes, sizeInFrames, 
        IRadSoundHalAudioFormat::Frames, pIRadSoundHalAudioFormat ) );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;
	m_refIRadMemoryObject = pIRadMemoryObject;
	m_Looping = looping;
    m_Streaming = streaming;
    m_SizeInFrames = sizeInFrames;
}

//========================================================================
// radSoundHalBufferWin::GetMemoryObject
//========================================================================

IRadMemoryObject * radSoundHalBufferWin::GetMemoryObject( void )
{
	return m_refIRadMemoryObject;
}

//========================================================================
// radSoundHalBufferWin::Clear
//========================================================================

void radSoundHalBufferWin::ClearAsync
( 
	unsigned int startPositionInFrames,
	unsigned int numberOfFrames,
	IRadSoundHalBufferClearCallback * pIRadSoundHalBufferClearCallback
)
{
    // Only worry about the busy buffers

    BufferData * pBufferIter = m_pBusyBufferDataList;

    while( pBufferIter != NULL )
    {
        ClearOpenALBuffer( pBufferIter->m_Buffer, startPositionInFrames, numberOfFrames );
        pBufferIter = pBufferIter->m_pNext;
    }

	if ( pIRadSoundHalBufferClearCallback != NULL )
	{
		pIRadSoundHalBufferClearCallback->OnBufferClearComplete( );
	}
}

//========================================================================
// radSoundHalBufferWin::ClearOpenALBuffer
//========================================================================

void radSoundHalBufferWin::ClearOpenALBuffer
(
    ALuint buffer,
    unsigned int offsetInFrames,
    unsigned int sizeInFrames
)
{
    if ( buffer != 0 )
    {
        rAssert( offsetInFrames < m_SizeInFrames );
        rAssert( ( offsetInFrames + sizeInFrames ) <= m_SizeInFrames );

        unsigned int offsetInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( offsetInFrames );
        unsigned int sizeInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( sizeInFrames );  

        unsigned char fillChar = ( m_refIRadSoundHalAudioFormat->GetBitResolution( ) == 8 ) ? 128 : 0;

        ::memset(static_cast<char*>(m_refIRadMemoryObject->GetMemoryAddress()) + offsetInBytes, fillChar, sizeInBytes);

        ALenum format = AL_FORMAT_MONO8;
        if (m_refIRadSoundHalAudioFormat->GetNumberOfChannels() > 1)
            format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
        else
            format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;

        alBufferData(buffer, format,
            m_refIRadMemoryObject->GetMemoryAddress(),
            m_refIRadMemoryObject->GetMemorySize(),
            m_refIRadSoundHalAudioFormat->GetSampleRate()
        );

        rAssertMsg(alGetError() == AL_NO_ERROR, "radSoundHalBufferWin::Clear Failed.\n");
    }
}

//========================================================================
// radSoundHalBufferWin::GetSizeInBytes
//========================================================================

unsigned int radSoundHalBufferWin::GetSizeInBytes( void )
{
    return m_refIRadSoundHalAudioFormat->FramesToBytes( m_SizeInFrames );
}

//========================================================================
// radSoundHalBufferWin::IsStreaming
//========================================================================

bool radSoundHalBufferWin::IsStreaming( void )
{
    return m_Streaming;
}

//========================================================================
// radSoundHalBufferWin::GetSizeInFrames
//========================================================================

unsigned int radSoundHalBufferWin::GetSizeInFrames( void )
{
    return m_SizeInFrames;
}

//========================================================================
// radSoundHalBufferWin::CreateOpenALBuffer
//========================================================================

void radSoundHalBufferWin::CreateOpenALBuffer
(
    bool support3DSound,
    ALuint * pBuffer
)
{
    alGenBuffers(1, pBuffer);
    rAssert(alGetError() == AL_NO_ERROR);

    ALenum format = AL_FORMAT_MONO8;
    if (m_refIRadSoundHalAudioFormat->GetNumberOfChannels() > 1)
        format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    else
        format = m_refIRadSoundHalAudioFormat->GetBitResolution() == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;

    rAssert(alGetError() == AL_NO_ERROR);
    alBufferData(*pBuffer, format,
        m_refIRadMemoryObject->GetMemoryAddress(),
        m_refIRadMemoryObject->GetMemorySize(),
        m_refIRadSoundHalAudioFormat->GetSampleRate()
    );

    rAssert(alGetError() == AL_NO_ERROR);
}

//========================================================================
// radSoundHalBufferWin::GetBufferData
//========================================================================

void radSoundHalBufferWin::GetBufferData
(
    bool positional,
    BufferData ** ppBufferData
)
{
    rAssert( ppBufferData != NULL );
    rAssert( * ppBufferData == NULL );

    if( positional == true )
    {
        * ppBufferData = m_pFreeBufferDataList_Pos;

        if( * ppBufferData != NULL )
        {
            RemoveFromList( * ppBufferData );
        }
    }
    else
    {
        * ppBufferData = m_pFreeBufferDataList_NonPos;

        if( * ppBufferData != NULL )
        {
            RemoveFromList( * ppBufferData );
        }
    }

    if( * ppBufferData == NULL )
    {
        // Otherwise prepare a new one

        ALuint buffer = NULL;
        CreateOpenALBuffer(positional, &buffer);
        rAssert( buffer != NULL );

        CreateBufferData( ppBufferData, buffer );
        rAssert( * ppBufferData != NULL );
   }

    if( m_Streaming == true )
    {
        // For streaming buffers, only be one buffer active at a time
    
        rAssert( m_pBusyBufferDataList == NULL );
    }

    // Add the buffer data to the busy list

    if( * ppBufferData != NULL )
    {
        AddToList( & m_pBusyBufferDataList, * ppBufferData );
    }
}

//========================================================================
// radSoundHalBufferWin::FreeBufferData
//========================================================================

void radSoundHalBufferWin::FreeBufferData
(
    bool positional,
    BufferData * pBufferData
)
{
    rAssert( pBufferData != NULL );

    if( pBufferData != NULL )
    {
        RemoveFromList( pBufferData );

        if( positional == true )
        {
            if( m_Streaming == true )
            {
                // Streaming buffers shouldn't be allocating a bunch of directsound buffers
                rAssert( m_pFreeBufferDataList_Pos == NULL );
            }

            AddToList( & m_pFreeBufferDataList_Pos, pBufferData );
        }
        else
        {
            if( m_Streaming == true )
            {
                // Streaming buffers shouldn't be allocating a bunch of directsound buffers
                rAssert( m_pFreeBufferDataList_NonPos == NULL );
            }

            AddToList( & m_pFreeBufferDataList_NonPos, pBufferData );
        }
    }
}

//========================================================================
// radSoundHalBufferWin::GetSoundSampleFormat
//========================================================================
    
IRadSoundHalAudioFormat * radSoundHalBufferWin::GetFormat( void )
{
    return m_refIRadSoundHalAudioFormat;
}

//========================================================================
// radSoundHalBufferWin::LoadAsync
//========================================================================

void radSoundHalBufferWin::LoadAsync
(
	IRadSoundHalDataSource * pIRadSoundHalDataSource,
	unsigned int bufferStartInFrames,
	unsigned int numberOfFrames,
	IRadSoundHalBufferLoadCallback * pIRadSoundHalBufferLoadCallback 
)
{
    // At this time there should really only be one direct sound buffer created and 
    // it should be busy.  Let's verify that.

    rAssert( m_pFreeBufferDataList_Pos == NULL );
    rAssert( m_pFreeBufferDataList_NonPos == NULL );
    rAssert( m_refIRadSoundHalBufferLoadCallback == NULL );

    m_LoadStartInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( bufferStartInFrames );
    m_refIRadSoundHalBufferLoadCallback = pIRadSoundHalBufferLoadCallback;

    m_pLockedLoadBuffer = static_cast< char* >(m_refIRadMemoryObject->GetMemoryAddress()) + m_LoadStartInBytes;
    m_LockedLoadBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( numberOfFrames );

    new( "radSoundBufferLoaderWin", RADMEMORY_ALLOC_TEMP ) radSoundBufferLoaderWin(
        static_cast< IRadSoundHalBuffer * >( this ),
        m_pLockedLoadBuffer,
        pIRadSoundHalDataSource,
        m_refIRadSoundHalAudioFormat,
        m_refIRadSoundHalAudioFormat->BytesToFrames( m_LockedLoadBytes ),
        this );
}

//========================================================================
// radSoundHalBufferWin::IsLooping
//========================================================================

bool radSoundHalBufferWin::IsLooping
(
    void
)
{
    return m_Looping;
}   

//========================================================================
// radSoundHalBufferWin::OnBufferLoadComplete
//========================================================================

void radSoundHalBufferWin::OnBufferLoadComplete( unsigned int dataSourceFrames )
{
    rAssert( m_refIRadSoundHalBufferLoadCallback != NULL );

    if( m_refIRadSoundHalBufferLoadCallback != NULL )
    {
        m_refIRadSoundHalBufferLoadCallback->OnBufferLoadComplete( dataSourceFrames );
        m_refIRadSoundHalBufferLoadCallback = NULL;
    }
}

//========================================================================
// radSoundHalBufferWin::CancelAsyncOperations
//========================================================================

void radSoundHalBufferWin::CancelAsyncOperations( void )
{
    radSoundBufferLoaderWin::CancelOperations( static_cast< IRadSoundHalBuffer * >( this ) );    

    if( m_refIRadSoundHalBufferLoadCallback != NULL )
    {
        rAssert( m_pFreeBufferDataList_NonPos == NULL ); // sanity
        rAssert( m_pFreeBufferDataList_Pos == NULL );

        m_pLockedLoadBuffer = NULL;
        m_LockedLoadBytes = 0;
        m_refIRadSoundHalBufferLoadCallback = NULL;
    }
}

//========================================================================
// radSoundHalBufferWin::GetMinTransferSizeInFrames
//========================================================================

unsigned int radSoundHalBufferWin::GetMinTransferSize( IRadSoundHalAudioFormat::SizeType sizeType )
{
    rAssert( m_refIRadSoundHalAudioFormat != NULL );

    //
    // Channels of data are eventually dma'd seperately to spu.
    // Therefore, transfers must occur in multiples of the optimal
    // dma multiple * the number of channels.
    //

    return m_refIRadSoundHalAudioFormat->ConvertSizeType( sizeType, 
        radMemorySpace_OptimalMultiple * m_refIRadSoundHalAudioFormat->GetNumberOfChannels( ),
        IRadSoundHalAudioFormat::Bytes );
}

//========================================================================
// radSoundHalBufferWin::CreateBufferData
//========================================================================

void radSoundHalBufferWin::CreateBufferData( BufferData ** ppBufferData, ALuint buffer )
{
    rAssert( s_refIRadMemoryPool != NULL );
    rAssert( ppBufferData != NULL );
    rAssert( * ppBufferData == NULL );
    rAssert( buffer != NULL );

    unsigned int numFreePoolAllocations;
    s_refIRadMemoryPool->GetStatus( NULL, & numFreePoolAllocations, NULL );

    if( numFreePoolAllocations == 0 )
    {
        // We're out of pool allocations.  
        // Destroy the least recently used BufferData and try getting memory again.

        BufferData * pFreeLRUBuffer = NULL;
        GetLeastRecentlyUsedFreeBufferData( & pFreeLRUBuffer );

        if( pFreeLRUBuffer != NULL )
        {
            DeleteBufferData( pFreeLRUBuffer );
            CreateBufferData( ppBufferData, buffer );
        }
        rAssertMsg( * ppBufferData != NULL, "radSound: Buffer data memory pool empty.  Increase max allocations with IRadSoundHalSystem::InitializeMemory()" );
    }
    else
    {
        * ppBufferData = ( BufferData * ) s_refIRadMemoryPool->GetMemory( );

        if( * ppBufferData != NULL )
        {
            ( * ppBufferData )->m_pNext = NULL;
            ( * ppBufferData )->m_pPrev = NULL;
            ( * ppBufferData )->m_pLRUNext = NULL;
            ( * ppBufferData )->m_pLRUPrev = NULL;
            ( * ppBufferData )->m_Buffer = buffer;
            ( * ppBufferData )->m_pListOwner = NULL;
            radSoundHalBufferWin::DebugInfo::s_Total++;
        }
    }
}

//========================================================================
// radSoundHalBufferWin::DeleteBufferData
//========================================================================

void radSoundHalBufferWin::DeleteBufferData( BufferData * pBufferData )
{
    rAssert( s_refIRadMemoryPool != NULL );
    rAssert( pBufferData != NULL );
    rAssert( pBufferData->m_pNext == NULL );
    rAssert( pBufferData->m_pPrev == NULL );
    rAssert( pBufferData->m_pLRUNext == NULL );
    rAssert( pBufferData->m_pLRUPrev == NULL );
    rAssert( pBufferData->m_pListOwner == NULL );
    rAssert( pBufferData->m_ppHead == NULL );

    pBufferData->m_pNext = NULL;
    pBufferData->m_pPrev = NULL;
    pBufferData->m_pLRUNext = NULL;
    pBufferData->m_pLRUPrev = NULL;
    pBufferData->m_pListOwner = NULL;
    pBufferData->m_ppHead = NULL;
    pBufferData->m_Buffer = NULL;
    s_refIRadMemoryPool->FreeMemory( ( void * ) pBufferData );
    radSoundHalBufferWin::DebugInfo::s_Total--;
}

//========================================================================
// radSoundHalBufferWin::AddToList
//========================================================================

void radSoundHalBufferWin::AddToList( BufferData ** ppListHead, BufferData * pBufferData )
{
    rAssert( pBufferData != NULL );

    // If this node is being added to a free list, also add it to the LRU list

    if( ppListHead != & m_pBusyBufferDataList )
    {
        pBufferData->m_pLRUNext = s_pLRUFreeBufferListHead;
        pBufferData->m_pLRUPrev = NULL;

        if( s_pLRUFreeBufferListHead != NULL )
        {
            s_pLRUFreeBufferListHead->m_pLRUPrev = pBufferData;
        }

        s_pLRUFreeBufferListHead = pBufferData;

        if( s_pLRUFreeBufferListTail != NULL )
        {   
            if( s_pLRUFreeBufferListTail->m_pLRUPrev == NULL )
            {
                s_pLRUFreeBufferListTail->m_pLRUPrev = pBufferData;
            }
        }
        else
        {
            s_pLRUFreeBufferListTail = pBufferData;
            rAssert( pBufferData->m_pLRUNext == NULL );
            rAssert( pBufferData->m_pLRUNext == NULL );
            rAssert( pBufferData == s_pLRUFreeBufferListHead );
        }
    }

    // Now add it to the specified list

    pBufferData->m_pNext = * ppListHead;
    pBufferData->m_pPrev = NULL;

    if( ( * ppListHead ) != NULL )
    {
        ( * ppListHead )->m_pPrev = pBufferData;
    }

    ( * ppListHead ) = pBufferData;
    pBufferData->m_ppHead = ppListHead;
    pBufferData->m_pListOwner = this;
    
    // Debug tracking

    if( ppListHead == & m_pFreeBufferDataList_Pos )
    {
        m_DebugInfo.m_FreeCountPos++;
        DebugInfo::s_TotalFreeCount++;
    }
    else if( ppListHead == & m_pFreeBufferDataList_NonPos )
    {
        m_DebugInfo.m_FreeCountNonPos++;
        DebugInfo::s_TotalFreeCount++;
    }
    else
    {
        rAssert( ppListHead == & m_pBusyBufferDataList );
        m_DebugInfo.m_BusyCount++;
        DebugInfo::s_TotalBusyCount++;
    }
}

//========================================================================
// radSoundHalBufferWin::RemoveFromList
//========================================================================

void radSoundHalBufferWin::RemoveFromList( BufferData * pBufferData )
{
    rAssert( pBufferData != NULL );

    if( pBufferData->m_pListOwner != this )
    {
        // A little funny but this makes stats tracking more possible
        pBufferData->m_pListOwner->RemoveFromList( pBufferData );
        return;
    }

    // If this node is being removed from a free list, also remove it from the LRU list

    BufferData ** ppListHead = pBufferData->m_ppHead;

    if( ppListHead != & m_pBusyBufferDataList )
    {
        if( pBufferData->m_pLRUPrev != NULL )
        {
            pBufferData->m_pLRUPrev->m_pLRUNext = pBufferData->m_pLRUNext;                
        }
        else
        {
            rAssert( s_pLRUFreeBufferListHead == pBufferData );
            s_pLRUFreeBufferListHead = pBufferData->m_pLRUNext;
        }

        if( pBufferData->m_pLRUNext != NULL )
        {
            pBufferData->m_pLRUNext->m_pLRUPrev = pBufferData->m_pLRUPrev;
        }
        else
        {
            rAssert( s_pLRUFreeBufferListTail == pBufferData );
            s_pLRUFreeBufferListTail = pBufferData->m_pLRUPrev;
        }
    }

    // Now remove from the specified list

    if( pBufferData->m_pPrev != NULL )
    {
        pBufferData->m_pPrev->m_pNext = pBufferData->m_pNext;
    }
    else
    {
        rAssert( ( * ppListHead ) == pBufferData );
        ( * ppListHead ) = pBufferData->m_pNext;
    }

    if( pBufferData->m_pNext != NULL )
    {
        pBufferData->m_pNext->m_pPrev = pBufferData->m_pPrev;
    }

    pBufferData->m_pPrev = NULL;
    pBufferData->m_pNext = NULL;
    pBufferData->m_pLRUNext = NULL;
    pBufferData->m_pLRUPrev = NULL;
    pBufferData->m_ppHead = NULL;
    pBufferData->m_pListOwner = NULL;

    // Debug tracking

    if( ppListHead == & m_pFreeBufferDataList_Pos )
    {
        m_DebugInfo.m_FreeCountPos--;
        DebugInfo::s_TotalFreeCount--;
    }
    else if( ppListHead == & m_pFreeBufferDataList_NonPos )
    {
        m_DebugInfo.m_FreeCountNonPos--;
        DebugInfo::s_TotalFreeCount--;
    }
    else
    {
        rAssert( ppListHead == & m_pBusyBufferDataList );
        m_DebugInfo.m_BusyCount--;
        DebugInfo::s_TotalBusyCount--;
    }
}

//========================================================================
// radSoundHalBufferWin::GetLeastRecentlyUsedFreeBufferData
//========================================================================

void radSoundHalBufferWin::GetLeastRecentlyUsedFreeBufferData( BufferData ** ppBufferData )
{
    rAssert( * ppBufferData == NULL );

    // Hang on to the LRU list tail then remove it from any lists

    ( * ppBufferData ) = s_pLRUFreeBufferListTail;

    if( * ppBufferData != NULL )
    {
        RemoveFromList( * ppBufferData );
    }
}

//========================================================================
// radSoundHalBufferWin::InitializeDuplicateBufferPool
//========================================================================

void radSoundHalBufferWin::InitializeBufferDataPool( unsigned int maxCount, radMemoryAllocator allocator )
{
    rAssert( s_refIRadMemoryPool == NULL );

    ::radMemoryCreatePool( & s_refIRadMemoryPool, sizeof( BufferData ), maxCount, 0, false, None, allocator, "radSoundHalBufferDataPool" );
    rAssert( s_refIRadMemoryPool != NULL );
}

//========================================================================
// radSoundHalBufferWin::TerminateDuplicateBufferPool
//========================================================================

void radSoundHalBufferWin::TerminateBufferDataPool( void )
{
    rAssert( s_refIRadMemoryPool != NULL );

    unsigned int elementSize, numberFree, numberAllocated;
    s_refIRadMemoryPool->GetStatus( & elementSize, & numberFree, & numberAllocated );
    rAssert( numberAllocated == 0 ); // Not all duplicate buffers were destroyed
    
    // Destroy the Pool
    s_refIRadMemoryPool = NULL;
}

//========================================================================
// ::radSoundCreateBufferWin
//========================================================================

IRadSoundHalBuffer * radSoundHalBufferCreate( radMemoryAllocator allocator )
{
	return new ( "radSoundHalBufferWin", allocator ) radSoundHalBufferWin( );
}

