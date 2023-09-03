//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include "pch.hpp"
#include <radsound.hpp>
#include "streamplayer.hpp"

// extern unsigned int g_GameLoops;

bool g_VoiceStoppingPlayingSilence = false;

//========================================================================
// radSoundStreamPlayer::radSoundStreamPlayer
//========================================================================

radSoundStreamPlayer::radSoundStreamPlayer( void )
	:
	m_State( IRadSoundStreamPlayer::NoSource ),
    m_QueueingSubState( Queueing_None ),
    m_LastPlaybackPositionInSamples( 0 ),
	m_OutstandingLoadSize( 0 ),
	m_SourceFramesRead( 0 ),
	m_EndFrameCounter( 0xFFFFFFFF ),
    m_LoadSkipLastFrame( false ),
    m_PollSkipLastFrame( false ),
    m_CurrentLoadBuffer( 0 ),
	m_InitializeInfo_Size( 0 ),
	m_InitializeInfo_SizeType( IRadSoundHalAudioFormat::Milliseconds )

{
    AddToUpdateList( );

    ::radStringCreate( & m_xIRadString_Name, GetThisAllocator( ) );

	m_xIRadSoundHalVoice = ::radSoundHalVoiceCreate( GetThisAllocator( ) );
}

//========================================================================
// radSoundStreamPlayer::~radSoundStreamPlayer
//========================================================================

radSoundStreamPlayer::~radSoundStreamPlayer( void )
{
    rDebugChannelPrintf(
        radSoundDebugChannel,
        "radsound: radSoundStreamPlayer destroyed: [%s]\n",
        m_xIRadString_Name->GetChars( ) );
}

//========================================================================
// radSoundStreamPlayer::InitializeAsync
//========================================================================

void radSoundStreamPlayer::InitializeAsync
(
	unsigned int size,
    IRadSoundHalAudioFormat::SizeType sizeType,
	IRadSoundHalMemoryRegion * pIRadSoundHalMemoryRegion,
    const char * pIdentifier
)
{
    rAssertMsg( pIdentifier != NULL, "You MUST name all of your stream players so we can track memory usage" );

	rAssert( size > 0 );
	rAssert( pIRadSoundHalMemoryRegion != NULL );

	m_InitializeInfo_Size = size;
	m_InitializeInfo_SizeType = sizeType;
	m_xInitializeInfo_MemRegion = pIRadSoundHalMemoryRegion;
    m_xIRadString_Name->Copy( pIdentifier );
}

//========================================================================
// radSoundStreamPlayer::Initialize
//========================================================================

void radSoundStreamPlayer::Initialize
(
	IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
	unsigned int size,
    IRadSoundHalAudioFormat::SizeType sizeType,
	IRadSoundHalMemoryRegion * pIRadSoundHalMemoryRegion,
    const char * pIdentifier
)
{
	rAssert( pIRadSoundHalAudioFormat != NULL );
    rAssert( pIRadSoundHalMemoryRegion != NULL );

    rAssertMsg( pIdentifier != NULL, "You MUST name all of your stream players so we can track memory usage" );

	m_InitializeInfo_Size = size;
	m_InitializeInfo_SizeType = sizeType;
	m_xInitializeInfo_MemRegion = pIRadSoundHalMemoryRegion;
    m_xIRadString_Name->Copy( pIdentifier );

	AllocateResources( pIRadSoundHalAudioFormat );
}

//========================================================================
// radSoundStreamPlayer::AllocateResources
//========================================================================

void radSoundStreamPlayer::AllocateResources( IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat )
{
	rAssert( pIRadSoundHalAudioFormat != NULL );

    //
    // If we are loading a new data source, check if it is the same format
    // if not, we reallocate and print out a usage warning.
    //

	if (m_xIRadSoundHalAudioFormat)
	{
        if (!m_xIRadSoundHalAudioFormat->Matches(pIRadSoundHalAudioFormat))
        {
            IRadSoundHalAudioFormat::Encoding oldEncoding = m_xIRadSoundHalAudioFormat->GetEncoding();
            IRadSoundHalAudioFormat::Encoding newEncoding = pIRadSoundHalAudioFormat->GetEncoding();
            unsigned int oldChannels = m_xIRadSoundHalAudioFormat->GetNumberOfChannels();
            unsigned int newChannels = pIRadSoundHalAudioFormat->GetNumberOfChannels();

            rTuneWarningMsg(false, "\n\n\nAUDIO: ERROR: Streamer buffer realloc due to format mismatch...\n\n\n");
        }
        else
        {
            for (int i = 0; i < RSD_STREAM_NUM_BUFFERS; i++)
                m_xIRadSoundHalBuffers[i]->ReSetAudioFormat(pIRadSoundHalAudioFormat);
            return;
        }
	}
    m_xIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;

	unsigned int sizeInFrames = pIRadSoundHalAudioFormat->ConvertSizeType(
		IRadSoundHalAudioFormat::Frames, m_InitializeInfo_Size, m_InitializeInfo_SizeType );

    unsigned int optimalFrameMultiple = pIRadSoundHalAudioFormat->BytesToFrames(
        radSoundHalDataSourceReadMultipleGet( ) );

    sizeInFrames = radMemoryRoundUp( sizeInFrames / RSD_STREAM_NUM_BUFFERS, optimalFrameMultiple );

    sizeInFrames = ::radSoundHalBufferCalculateMemorySize( IRadSoundHalAudioFormat::Frames,
        sizeInFrames, IRadSoundHalAudioFormat::Frames, pIRadSoundHalAudioFormat );

	unsigned int sizeInBytes = pIRadSoundHalAudioFormat->FramesToBytes( sizeInFrames );

    for (int i = 0; i < RSD_STREAM_NUM_BUFFERS; i++)
    {
        rTunePrintf("AUDIO: Allocating stream resource in sound memory: [0x%x] Bytes\n", sizeInBytes);

        rAssert((sizeInBytes % pIRadSoundHalAudioFormat->GetFrameSizeInBytes()) == 0);
        rAssert(m_xInitializeInfo_MemRegion != NULL);

        ref< IRadMemoryObject > xIRadMemoryObject;

        m_xInitializeInfo_MemRegion->CreateMemoryObject(
            &xIRadMemoryObject, sizeInBytes, m_xIRadString_Name->GetChars());

        // Check if we are out of memory, this would be bad.

        if (xIRadMemoryObject != NULL)
        {
            m_xIRadSoundHalBuffers[i] = ::radSoundHalBufferCreate(GetThisAllocator());
            m_xIRadSoundHalBuffers[i]->Initialize(pIRadSoundHalAudioFormat, xIRadMemoryObject, sizeInFrames, false, true);
        }
        else
        {
            rAssertMsg(false, "Out of memory");
        }
    }
}

//========================================================================
// radSoundStreamPlayer::Play
//========================================================================

void radSoundStreamPlayer::Play( void )
{
	if ( m_State == IRadSoundStreamPlayer::Paused )
	{
        // If we are paused, we are all queued up and ready to go.

        rAssert( m_xIRadSoundHalVoice->IsPlaying( ) == false );

		m_xIRadSoundHalVoice->Play( );

		SetState( IRadSoundStreamPlayer::Playing );
	}
	else if ( m_State == IRadSoundStreamPlayer::Queueing )
	{
        // If we are queueing, we can't start the voice, but just set
        // a flag signalling us to start playing immediately after
        // we have queued up.

		SetState( IRadSoundStreamPlayer::QueuedPlay );
	}
}

//========================================================================
// radSoundStreamPlayer::SetState
//========================================================================

void radSoundStreamPlayer::SetState( IRadSoundStreamPlayer::State state )
{
    m_State = state;
}

//========================================================================
// radSoundStreamPlayer::Stop
//========================================================================

void radSoundStreamPlayer::Stop( void )
{
    StopVoice( false );
    if ( m_State == IRadSoundStreamPlayer::QueuedPlay )
    {
        SetState( IRadSoundStreamPlayer::Queueing );
    }
    else if( m_State == IRadSoundStreamPlayer::Playing )
    {
        SetState( IRadSoundStreamPlayer::Paused );
    }
}

void radSoundStreamPlayer::StopVoice( bool playingSilence )
{
    g_VoiceStoppingPlayingSilence = playingSilence;
    m_xIRadSoundHalVoice->Stop( );
    g_VoiceStoppingPlayingSilence = false;
}

//========================================================================
// radSoundStreamPlayer::IsPlaying
//========================================================================

bool radSoundStreamPlayer::IsPlaying( void )
{
	return ( m_State == IRadSoundStreamPlayer::Playing ) ||
           ( m_State == IRadSoundStreamPlayer::QueuedPlay );
}

//========================================================================
// radSoundStreamPlayer::SetDataSource
//========================================================================

void radSoundStreamPlayer::SetDataSource( IRadSoundHalDataSource * pIRadSoundHalDataSource )
{
    // rDebugPrintf( "SetDataSource: Gl:[%d] Ptr: [0x%x]\n", g_GameLoops, pIRadSoundHalDataSource );

    // Stop and reset the voice.

	StopVoice( false );
    m_xIRadSoundHalVoice->SetPlaybackPositionInSamples( 0 );

    //
    // Stop and reset the buffers.
    //

    for (int i = 0; i < RSD_STREAM_NUM_BUFFERS; i++)
    {
        if (m_xIRadSoundHalBuffers[i] != NULL)
        {
            m_xIRadSoundHalBuffers[i]->CancelAsyncOperations();
        }
    }

	m_xIRadSoundHalDataSource = pIRadSoundHalDataSource;

    m_LastPlaybackPositionInSamples = 0;
	m_SourceFramesRead = 0;
	m_EndFrameCounter = 0xFFFFFFFF;
    m_OutstandingLoadSize = 0;
    m_CurrentLoadBuffer = 0;
    m_LoadSkipLastFrame = false;

	if ( m_xIRadSoundHalDataSource != NULL )
	{
		SetState( IRadSoundStreamPlayer::Queueing );
	    m_QueueingSubState = Queueing_WaitingForDataSourceToInitialize;                
	}
	else
	{
        m_QueueingSubState = Queueing_None;
		SetState( IRadSoundStreamPlayer::NoSource );
	}

    Update( 0 );
}

//========================================================================
// radSoundStreamPlayer::OnBufferLoadComplete
//========================================================================

void radSoundStreamPlayer::OnBufferLoadComplete(
    IRadSoundHalBuffer* pIRadSoundHalBuffer,
    unsigned int dataSourceFrames )
{
	rAssert( m_OutstandingLoadSize > 0 );
	rAssert( m_OutstandingLoadSize >= dataSourceFrames );

	if ( m_OutstandingLoadSize > dataSourceFrames )
	{
        rAssert( m_EndFrameCounter == 0xFFFFFFFF );
	
        m_EndFrameCounter = pIRadSoundHalBuffer->GetSizeInFrames( ) -
            ( m_OutstandingLoadSize - dataSourceFrames );
            
	}

    //
    // We assume that even though less frames were available than requested,
    // the bufferloader filled the remaining frames with silence
    //

    // But we still need to keep track of the amount of actual data
    // read.

	m_SourceFramesRead += dataSourceFrames;

    // Reset loading "flag"

	m_OutstandingLoadSize = 0;

    // rDebugPrintf( "Load Complete: Gl:[%d]\n", g_GameLoops );

    m_xIRadSoundHalVoice->QueueBuffer(pIRadSoundHalBuffer);
}

//========================================================================
// radSoundStreamPlayer::Update
//========================================================================

void radSoundStreamPlayer::Update( unsigned int elapsedTime )
{
    // Spin the state machine until we have to wait for IO.
    
    IRadSoundStreamPlayer::State prevState;
    QueueingSubState             prevQueueingSubState;
    unsigned int                 prevOutstandingLoadSize;

    do
    {
        prevState                = m_State;
        prevQueueingSubState     = m_QueueingSubState;
        prevOutstandingLoadSize  = m_OutstandingLoadSize;

        ServiceStateMachine( );

    } while( ( m_State != prevState ) ||
             ( m_QueueingSubState != prevQueueingSubState ) ||
             ( prevOutstandingLoadSize != m_OutstandingLoadSize ) );

}

//========================================================================
// radSoundStreamPlayer::ServiceStateMachine
//========================================================================

void radSoundStreamPlayer::ServiceStateMachine( void )
{
	if ( m_State == IRadSoundStreamPlayer::NoSource )
	{
        //
        // We are idle, no data source to play, nothing to do.
        //
    }
    else if ( m_State == IRadSoundStreamPlayer::Queueing || m_State == IRadSoundStreamPlayer::QueuedPlay )
    {
        // There are two queing states, we are either waiting for the data
        // source to initialize, or we are waiting for the fist block to load

        if ( m_QueueingSubState == Queueing_WaitingForDataSourceToInitialize )
        {
		    if ( m_xIRadSoundHalDataSource->GetState( ) != IRadSoundHalDataSource::Initialized )
		    {
			    // Do nothing, the data source is still not initialized
		    }
		    else
		    {
                // Ok the data source is done, allocate our resources--this
                // function might do nothing if our buffer is aready allocated.

			    AllocateResources( m_xIRadSoundHalDataSource->GetFormat( ) );
                m_QueueingSubState = Queueing_LoadingFirstBlock;
		    }
        }
        else if ( m_QueueingSubState == Queueing_LoadingFirstBlock )
        {
            // Check to see if we are finished loading the first block

		    if ( m_xIRadSoundHalVoice->GetQueuedBuffers() > 0 )
		    {
                // Here we have queued up a buffer worth of data, so move on
                // to one of the non-queuing states and start the voice if
                // we are in "queuedplay" mode.

                m_QueueingSubState = Queueing_None;

	            if ( m_State == IRadSoundStreamPlayer::Queueing )
	            {
                    SetState( IRadSoundStreamPlayer::Paused );
	            }
	            else if ( m_State == IRadSoundStreamPlayer::QueuedPlay )
	            {
                    rAssert( m_xIRadSoundHalVoice->IsPlaying( ) == false );

                    m_xIRadSoundHalVoice->Play( );

                    SetState( IRadSoundStreamPlayer::Playing );
	            }
    
                // rDebugPrintf( "Queued: Gl:[%d]\n", g_GameLoops );

            }
            else
            {
                // Fist block is still not loaded, keep loading.

                ServiceLoad( );
            }
        }
        else
        {
            rAssert( false ); // ooops logic problem in streamer!
        }
    }
    else if ( m_State == IRadSoundStreamPlayer::Playing )
    {
        if ( ServicePlay( ) )
        {
            ServiceLoad( ); // Just keep the buffer full
        }
    }
    else if ( m_State == IRadSoundStreamPlayer::Paused )
    {
        ServiceLoad( ); // And keep the buffer full even if we are paused.
    }
}

//========================================================================
// radSoundStreamPlayer::ServicePlay
//========================================================================

bool radSoundStreamPlayer::ServicePlay( void )
{
    unsigned int sizeInSamples = m_xIRadSoundHalAudioFormat->ConvertSizeType(
        IRadSoundHalAudioFormat::Samples, m_InitializeInfo_Size, m_InitializeInfo_SizeType);
    unsigned int playbackPositionInSamples = m_xIRadSoundHalVoice->GetPlaybackPositionInSamples( );
    unsigned int sourceSamplesLoaded = m_xIRadSoundHalAudioFormat->FramesToSamples( m_SourceFramesRead );
    unsigned int samplesPlayedThisFrame = playbackPositionInSamples - m_LastPlaybackPositionInSamples;

    if ( samplesPlayedThisFrame > sizeInSamples / RSD_STREAM_NUM_BUFFERS )
    {
        if ( m_PollSkipLastFrame == false )
        {
            #ifndef RAD_DEBUG
                rTunePrintf( "radsound: TRC Violation: Streamer: [%s] is skipping (polling latency).\n", m_xIRadString_Name->GetChars( ) );
            #endif
            
            m_PollSkipLastFrame = true;
        }   
    }
    else
    {
        m_PollSkipLastFrame = false;
    }
            
    if ( sourceSamplesLoaded > 0 && playbackPositionInSamples >= sourceSamplesLoaded )
    {
        if ( m_EndFrameCounter == 0xFFFFFFFF )
        {
            if ( m_LoadSkipLastFrame == false )
            {
                m_LoadSkipLastFrame = true;
                #ifndef RAD_DEBUG                
                    rTunePrintf( "radsound: TRC Violation: Streamer: [%s] is skipping (loading latency).\n", m_xIRadString_Name->GetChars( ) );
                #endif
            }
        }
        else
        {
            StopVoice( true ); // true means don't print out trc warning
		    SetDataSource( NULL );
		    return false;
        }
    }
    else
    {
        m_LoadSkipLastFrame = false;
    }

    m_LastPlaybackPositionInSamples = playbackPositionInSamples;
    
    return true;
}

//========================================================================
// radSoundStreamPlayer::ServiceLoad
//========================================================================

void radSoundStreamPlayer::ServiceLoad( void )
{
    // If we currently have an outstanding load or clear, we can't issue
    // another one until it is done so just do nothing for now.
    if (m_OutstandingLoadSize == 0 && m_EndFrameCounter == 0xFFFFFFFF &&
        m_xIRadSoundHalVoice->GetQueuedBuffers() < RSD_STREAM_NUM_BUFFERS)
    {
        m_CurrentLoadBuffer++;
        m_CurrentLoadBuffer %= RSD_STREAM_NUM_BUFFERS;

        IRadSoundHalBuffer* pBuffer = m_xIRadSoundHalBuffers[m_CurrentLoadBuffer];
        m_OutstandingLoadSize = pBuffer->GetSizeInFrames();
        pBuffer->LoadAsync(
		    m_xIRadSoundHalDataSource,
		    0,
            m_OutstandingLoadSize,
            this );
    }
}

//========================================================================
// radSoundStreamPlayer::GetPlaybackTimeInSamples
//========================================================================

unsigned int radSoundStreamPlayer::GetPlaybackTimeInSamples( void )
{
    return m_xIRadSoundHalVoice->GetPlaybackPositionInSamples();
}

//========================================================================
// radSoundStreamPlayer::SetPriority
//========================================================================

void radSoundStreamPlayer::SetPriority( unsigned int priority )
{
	m_xIRadSoundHalVoice->SetPriority( priority );
}

//========================================================================
// radSoundStreamPlayer::GetPriority
//========================================================================

unsigned int radSoundStreamPlayer::GetPriority( void )
{
	return m_xIRadSoundHalVoice->GetPriority( );
}

//========================================================================
// radSoundStreamPlayer::SetMuted
//========================================================================

void  radSoundStreamPlayer::SetMuted( bool muted )
{
	m_xIRadSoundHalVoice->SetMuted( muted );
}

//========================================================================
// radSoundStreamPlayer::GetMuted
//========================================================================

bool  radSoundStreamPlayer::GetMuted( void )
{
	return m_xIRadSoundHalVoice->GetMuted( );
}

//========================================================================
// radSoundStreamPlayer::SetVolume
//========================================================================

void  radSoundStreamPlayer::SetVolume( float volume )
{
	m_xIRadSoundHalVoice->SetVolume( volume );
}

//========================================================================
// radSoundStreamPlayer::GetVolume
//========================================================================

float radSoundStreamPlayer::GetVolume( void )
{
	return m_xIRadSoundHalVoice->GetVolume( );
}

//========================================================================
// radSoundStreamPlayer::SetTrim
//========================================================================

void  radSoundStreamPlayer::SetTrim( float trim )
{
	m_xIRadSoundHalVoice->SetTrim( trim );
}

//========================================================================
// radSoundStreamPlayer::GetTrim
//========================================================================

float radSoundStreamPlayer::GetTrim( void )
{
	return m_xIRadSoundHalVoice->GetTrim( );
}

//========================================================================
// radSoundStreamPlayer::SetPitch
//========================================================================

void  radSoundStreamPlayer::SetPitch( float pitch )
{
	m_xIRadSoundHalVoice->SetPitch( pitch );
}

//========================================================================
// radSoundStreamPlayer::GetPitch
//========================================================================

float radSoundStreamPlayer::GetPitch( void )
{
	return m_xIRadSoundHalVoice->GetPitch( );
}

//========================================================================
// radSoundStreamPlayer::SetPan
//========================================================================

void  radSoundStreamPlayer::SetPan( float pan )
{
	m_xIRadSoundHalVoice->SetPan( pan );
}

//========================================================================
// radSoundStreamPlayer::GetPan
//========================================================================

float radSoundStreamPlayer::GetPan( void )
{
	return m_xIRadSoundHalVoice->GetPan( );
}

//========================================================================
// radSoundStreamPlayer::SetAuxMode
//========================================================================

void  radSoundStreamPlayer::SetAuxMode( unsigned int aux, radSoundAuxMode mode )
{
	m_xIRadSoundHalVoice->SetAuxMode( aux, mode );

}

//========================================================================
// radSoundStreamPlayer::GetAuxMode
//========================================================================

radSoundAuxMode radSoundStreamPlayer::GetAuxMode( unsigned int aux )
{
	return m_xIRadSoundHalVoice->GetAuxMode( aux );
}

//========================================================================
// radSoundStreamPlayer::SetAuxGain
//========================================================================

void  radSoundStreamPlayer::SetAuxGain( unsigned int aux, float gain )
{
	m_xIRadSoundHalVoice->SetAuxGain( aux, gain );
}

//========================================================================
// radSoundStreamPlayer::GetAuxGain
//========================================================================

float radSoundStreamPlayer::GetAuxGain( unsigned int aux )
{
	return m_xIRadSoundHalVoice->GetAuxGain( aux );
}

//========================================================================
// radSoundStreamPlayer::SetPositionalGroup
//========================================================================

void radSoundStreamPlayer::SetPositionalGroup( IRadSoundHalPositionalGroup * pIRshpg )
{
	m_xIRadSoundHalVoice->SetPositionalGroup( pIRshpg );
}

//========================================================================
// radSoundStreamPlayer::GetPositionalGroup
//========================================================================

IRadSoundHalPositionalGroup * radSoundStreamPlayer::GetPositionalGroup( void )
{
	return m_xIRadSoundHalVoice->GetPositionalGroup( );
}

//========================================================================
// radSoundStreamPlayer::GetDataSource
//========================================================================

IRadSoundHalDataSource * radSoundStreamPlayer::GetDataSource( void )
{
	return m_xIRadSoundHalDataSource;
}

//========================================================================
// radSoundStreamPlayer::GetFormat
//========================================================================

IRadSoundHalAudioFormat * radSoundStreamPlayer::GetFormat( void )
{
    rAssertMsg(m_xIRadSoundHalAudioFormat != NULL, "Can't get format of streamer until it is initialized");

    return m_xIRadSoundHalAudioFormat;
}

//========================================================================
// radSoundStreamPlayer::GetState
//========================================================================

IRadSoundStreamPlayer::State radSoundStreamPlayer::GetState( )
{
    return m_State;
}

//========================================================================
// ::radSoundStreamPlayerCreate
//========================================================================

IRadSoundStreamPlayer * radSoundStreamPlayerCreate( radMemoryAllocator allocator )
{
	return new ( "radSoundStreamPlayer", allocator ) radSoundStreamPlayer;
}
