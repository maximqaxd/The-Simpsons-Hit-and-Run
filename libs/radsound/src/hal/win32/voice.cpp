//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

//========================================================================
// Include Files
//========================================================================

#include "pch.hpp"
#include "voice.hpp"
#include "listener.hpp"
#include "system.hpp"

//============================================================================
// Static Initialization
//============================================================================

radSoundHalVoiceWin * radSoundHalVoiceWin::s_pLinkedClassHead = NULL;
radSoundHalVoiceWin * radSoundHalVoiceWin::s_pLinkedClassTail = NULL;

//========================================================================
// radSoundHalVoiceWin::radSoundHalVoiceWin
//========================================================================

radSoundHalVoiceWin::radSoundHalVoiceWin( void )
    :
	m_Priority( 5 ),
    m_SourceSamplesPlayed( 0 ),
    m_Pitch( 1.0f ),
    m_Volume( 1.0f ),
    m_MuteFactor( 1.0f ),
    m_Trim( 1.0f ),
	m_xRadSoundHalPositionalGroup( NULL )
{
    alGenSources(1, &m_Source);
}

//========================================================================
// radSoundHalVoiceWin::~radSoundHalVoiceWin
//========================================================================

radSoundHalVoiceWin::~radSoundHalVoiceWin
(
    void
)
{
    //
    // Tell our buffer object that we are done with the voice/(buffer), our
    // buffer object manages the lifetime if its voices.
    //

    Stop( );

	if ( m_xRadSoundHalPositionalGroup != NULL )
	{
		m_xRadSoundHalPositionalGroup->RemovePositionalEntity( this );
		m_xRadSoundHalPositionalGroup;
	}

    if (m_Source)
    {
        alDeleteSources(1, &m_Source);
    }
}

void radSoundHalVoiceWin::SetPriority( unsigned int priority )
{
	m_Priority = priority;
}

unsigned int radSoundHalVoiceWin::GetPriority( void )
{
	return m_Priority;
}

//========================================================================
// radSoundHalVoiceWin::SetBuffer
//========================================================================

void radSoundHalVoiceWin::SetBuffer( IRadSoundHalBuffer * pIRadSoundHalBuffer )
{
    Stop( );

    m_xRadSoundHalBufferWin = NULL;
    m_SourceSamplesPlayed = 0;

	ref< IRadSoundHalAudioFormat > pOldIRadSoundHalAudioFormat = m_xIRadSoundHalAudioFormat;
    m_xIRadSoundHalAudioFormat = NULL;

    if ( pIRadSoundHalBuffer != NULL )
    {
        m_xRadSoundHalBufferWin = static_cast< radSoundHalBufferWin * >( pIRadSoundHalBuffer );
        rAssert( m_xRadSoundHalBufferWin != NULL );

        alSourcei(m_Source, AL_BUFFER, m_xRadSoundHalBufferWin->GetBuffer());

        // Now get the format of the buffer, we'll just store it here
        m_xIRadSoundHalAudioFormat = m_xRadSoundHalBufferWin->GetFormat( );

		//
		// The new format had better be the same as the old one 
		// (if the old one isn't null
		//
		rAssert
		( 
			pOldIRadSoundHalAudioFormat != NULL ?
			m_xIRadSoundHalAudioFormat->Matches( pOldIRadSoundHalAudioFormat ) :
			true
		);
    }
}

void radSoundHalVoiceWin::QueueBuffer(IRadSoundHalBuffer* pIRadSoundHalBuffer)
{
    ALint buffersProcessed;
    alGetSourcei(m_Source, AL_BUFFERS_PROCESSED, &buffersProcessed);
    rAssert(alGetError() == AL_NO_ERROR);

    // Unqueue all processed buffers before queueing new ones
    for (int i = 0; i < buffersProcessed; i++)
    {
        ALuint buffer;
        alSourceUnqueueBuffers(m_Source, 1, &buffer);
        rAssert(alGetError() == AL_NO_ERROR);

        // Calculate the number of samples in this buffer to
        // keep track of the current playback position
        ALint size, bits, channels;
        alGetBufferi(buffer, AL_SIZE, &size);
        alGetBufferi(buffer, AL_BITS, &bits);
        alGetBufferi(buffer, AL_CHANNELS, &channels);
        rAssert(alGetError() == AL_NO_ERROR);
        m_SourceSamplesPlayed += (size / (bits / 8)) / channels;

        if (m_xRadSoundHalBufferWin->IsStreaming())
        {
            // Delete the buffer since its data is no longer needed
            alDeleteBuffers(1, &buffer);
            rAssert(alGetError() == AL_NO_ERROR);
        }
    }

    ref< IRadSoundHalAudioFormat > pOldIRadSoundHalAudioFormat = m_xIRadSoundHalAudioFormat;
    m_xIRadSoundHalAudioFormat = NULL;
    m_xRadSoundHalBufferWin = static_cast< radSoundHalBufferWin * >( pIRadSoundHalBuffer );

    // Switching a voice between streaming and non-streaming on-the-fly has not been tested
    rAssert(m_xRadSoundHalBufferWin->IsStreaming());

    if (pIRadSoundHalBuffer != NULL)
    {
        // Now get the format of the buffer, we'll just store it here
        m_xIRadSoundHalAudioFormat = pIRadSoundHalBuffer->GetFormat( );

		//
		// The new format had better be the same as the old one 
		// (if the old one isn't null
		//
		rAssert
		( 
			pOldIRadSoundHalAudioFormat != NULL ?
			m_xIRadSoundHalAudioFormat->Matches( pOldIRadSoundHalAudioFormat ) :
			true
		);
        
        radSoundHalBufferWin * pRadSoundHalBufferWin = static_cast< radSoundHalBufferWin * >( pIRadSoundHalBuffer );
        ALuint buffer = pRadSoundHalBufferWin->GetBuffer();
        alSourceQueueBuffers(m_Source, 1, &buffer);
        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::QueueBuffer failed");
    }
}

int radSoundHalVoiceWin::GetQueuedBuffers( void )
{
    ALint buffersQueued;
    alGetSourcei(m_Source, AL_BUFFERS_QUEUED, &buffersQueued);
    if ( IsHardwarePlaying( ) == true )
    {
        ALint buffersProcessed;
        alGetSourcei(m_Source, AL_BUFFERS_PROCESSED, &buffersProcessed);
        buffersQueued -= buffersProcessed;
    }
    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::GetQueuedBuffers failed");
    return buffersQueued;
}

void radSoundHalVoiceWin::Play( )
{
    if (IsHardwarePlaying( ) == false)
    {
        alSourcei(m_Source, AL_LOOPING, m_xRadSoundHalBufferWin && m_xRadSoundHalBufferWin->IsLooping());
        alSourcePlay(m_Source);
        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::Play failed");
    }
}

void radSoundHalVoiceWin::Stop( void )
{
    if (IsHardwarePlaying( ) == true)
    {
#ifdef RAD_DEBUG
        extern bool g_VoiceStoppingPlayingSilence;

        if ( g_VoiceStoppingPlayingSilence == false )
        {
            if ( ( m_Trim * m_Volume ) > 0.0f )
            {
                rDebugPrintf( "radsound: TRC Violation: Voice stopped while playing and (trim * volume) > 0.0f\n" );
            }
        }
#endif // RAD_DEBUG

        alSourceStop(m_Source);

        if (m_xRadSoundHalBufferWin->IsStreaming())
        {
            int buffersProcessed;
            alGetSourcei(m_Source, AL_BUFFERS_PROCESSED, &buffersProcessed);
            for (int i = 0; i < buffersProcessed; i++)
            {
                ALuint buffer;
                alSourceUnqueueBuffers(m_Source, 1, &buffer);
                alDeleteBuffers(1, &buffer);
            }
        }

        rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::Stop failed");
    }
}

bool radSoundHalVoiceWin::IsPlaying( void )
{
    return IsHardwarePlaying( );
}

unsigned int radSoundHalVoiceWin::GetPlaybackPositionInSamples( void )
{
    ALint currentPosition = 0;
    alGetSourcei(m_Source, AL_SAMPLE_OFFSET, &currentPosition);
    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::GetPlaybackPositionInSamples failed");

    return m_SourceSamplesPlayed + currentPosition;
}

void radSoundHalVoiceWin::SetPlaybackPositionInSamples( unsigned int positionInSamples )
{
    alSourcei(m_Source, AL_SAMPLE_OFFSET, positionInSamples);
    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::SetPlaybackPositionInSamples failed");
}

void radSoundHalVoiceWin::SetMuted( bool muted)
{
    if ( muted != GetMuted( ) )
    {
        m_MuteFactor = muted ? 0.0f : 1.0f;
        SetVolumeInternal( );
    }
}

bool radSoundHalVoiceWin::GetMuted( void )
{
    return m_MuteFactor == 0.0f ? true : false;
}

void radSoundHalVoiceWin::SetVolume( float volume )
{
	::radSoundVerifyAnalogVolume( volume );

    if ( volume != m_Volume )
    {
        ::radSoundVerifyChangeThreshold(
            IsHardwarePlaying( ), "Volume", volume, m_Volume, radSoundVolumeChangeThreshold );

		m_Volume = volume;

        SetVolumeInternal( );
    }

}

float radSoundHalVoiceWin::GetVolume( void )
{
    return m_Volume;
}

void radSoundHalVoiceWin::SetTrim( float trim )
{
	::radSoundVerifyAnalogVolume( trim );

    if ( m_Trim != trim )
    {
        ::radSoundVerifyChangeThreshold(
            IsHardwarePlaying( ), "Trim", trim, m_Trim, radSoundVolumeChangeThreshold );

        m_Trim = trim;

        SetVolumeInternal( );
    }
}
    
float radSoundHalVoiceWin::GetTrim( void )
{
    return m_Trim;
}

void radSoundHalVoiceWin::SetPitch( float pitch )
{
    ::radSoundVerifyAnalogPitch( pitch );

    if ( m_Pitch != pitch )
    {
        m_Pitch = pitch;

		SetPitchInternal( );
    }
}

float radSoundHalVoiceWin::GetPitch( void )
{
    return m_Pitch;
}

void radSoundHalVoiceWin::SetPan( float pan )
{
    ::radSoundVerifyAnalogPan( pan );

    rWarningMsg(false, "voice::SetPan not available in win32");
}

float radSoundHalVoiceWin::GetPan( void )
{
    rWarningMsg(false, "voice::GetPan not available in win32");
    return 0.0f;
}

radSoundAuxMode radSoundHalVoiceWin::GetAuxMode( unsigned int aux )
{
    rWarningMsg( false, "voice::GetAuxMode not available in win32" );
    return radSoundAuxMode_PreFader;
}

void radSoundHalVoiceWin::SetAuxMode( unsigned int aux, radSoundAuxMode  mode )
{
    rWarningMsg( false, "voice::SetAuxMode not available in win32" );
}

float radSoundHalVoiceWin::GetAuxGain( unsigned int aux )
{
    rWarningMsg( false, "voice::GetAuxGain not available in win32" );
    return 1.0f;
}

void radSoundHalVoiceWin::SetAuxGain( unsigned int aux, float gain )
{
    rWarningMsg( false, "voice::SetAuxGain not available in win32" );
}

//========================================================================
// Function radSoundHalVoiceWin::IsHardwarePlaying
//========================================================================

bool radSoundHalVoiceWin::IsHardwarePlaying( void )
{
    ALint state;
    alGetSourcei(m_Source, AL_SOURCE_STATE, &state);
    rWarningMsg( alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::IsHardwarePlaying failed");

    // Check our internal flag of the last known "play state", if our flag
    // is playing but the hardware voice has stopped it means we haven't notified
    // the client that the voice was done

    return ( state == AL_PLAYING );
}

//========================================================================
// radSoundHalVoiceWin::SetVolumeInternal
//========================================================================

void radSoundHalVoiceWin::SetVolumeInternal( void )
{
    float volume = m_Trim * m_Volume * m_MuteFactor;

	alSourcef( m_Source, AL_GAIN, ::radSoundVolumeDbToHardwareWin( ::radSoundVolumeAnalogToDb( volume ) ) );

    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::SetVolumeInternal failed!");
}

//========================================================================
// radSoundHalVoiceWin::SetPitchInternal
//========================================================================

void radSoundHalVoiceWin::SetPitchInternal( void )
{
    ::radSoundVerifyAnalogPitch(m_Pitch);

    alSourcef(m_Source, AL_PITCH, m_Pitch);

    rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::SetPitchInternal failed!");
}

//========================================================================
// radSoundHalVoiceWin::SetPositionalGroup
//========================================================================

/* virtual */ void radSoundHalVoiceWin::SetPositionalGroup
( 
	IRadSoundHalPositionalGroup * pIRadSoundHalPositionalGroup 
)
{
	radSoundHalPositionalGroup * pRadSoundHalPositionalGroup
		= dynamic_cast< radSoundHalPositionalGroup * >(
			pIRadSoundHalPositionalGroup );

    if ( pRadSoundHalPositionalGroup != m_xRadSoundHalPositionalGroup )
    {
	    if ( pRadSoundHalPositionalGroup != m_xRadSoundHalPositionalGroup )
        {
		    if ( m_xRadSoundHalPositionalGroup != NULL )
		    {
			    m_xRadSoundHalPositionalGroup->RemovePositionalEntity( this );
		    }

		    m_xRadSoundHalPositionalGroup = pRadSoundHalPositionalGroup;

		    if ( m_xRadSoundHalPositionalGroup != NULL )
		    {
			    m_xRadSoundHalPositionalGroup->AddPositionalEntity( this );
		    }
	    }

        ref<radSoundHalSystem> refSystem = radSoundHalSystem::GetInstance();
        for (unsigned int i = 0; i < refSystem->GetNumAuxSends(); i++)
        {
            alSource3i(m_Source, AL_AUXILIARY_SEND_FILTER,
                refSystem->GetOpenALAuxSlot(i),
                i, NULL);
            rWarningMsg(alGetError() == AL_NO_ERROR, "Failed to set the source aux send filter");
        }
	}
}

//========================================================================
// radSoundHalVoiceWin::GetPositionalGroup
//========================================================================

/* virtual */ IRadSoundHalPositionalGroup * radSoundHalVoiceWin::GetPositionalGroup
(	
	void 
)
{
	return m_xRadSoundHalPositionalGroup;
}


//========================================================================
// radSoundHalVoiceWin::SetPositionalGroup
//========================================================================

/* virtual */ void radSoundHalVoiceWin::OnApplyPositionalInfo( float listenerRolloffFactor )
{
	SetVolumeInternal( );

    radSoundHalPositionalGroup* p = m_xRadSoundHalPositionalGroup;

    alSource3f(m_Source, AL_POSITION, p->m_Position.m_x, p->m_Position.m_y, -p->m_Position.m_z);
    alSource3f(m_Source, AL_VELOCITY, p->m_Velocity.m_x, p->m_Velocity.m_y, -p->m_Velocity.m_z);
    alSource3f(m_Source, AL_DIRECTION, p->m_Direction.m_x, p->m_Direction.m_y, -p->m_Direction.m_z);
    alSourcef(m_Source, AL_CONE_INNER_ANGLE, p->m_ConeOuterAngle);
    alSourcef(m_Source, AL_CONE_OUTER_ANGLE, p->m_ConeInnerAngle);
    alSourcef(m_Source, AL_CONE_OUTER_GAIN, p->m_ConeOuterGain);
    alSourcef(m_Source, AL_REFERENCE_DISTANCE, p->m_ReferenceDistance);
    alSourcef(m_Source, AL_MAX_DISTANCE, p->m_MaxDistance);
    alSourcef(m_Source, AL_ROLLOFF_FACTOR, listenerRolloffFactor);

	rWarningMsg(alGetError() == AL_NO_ERROR, "radSoundHalVoiceWin::OnApplyPositionalInfo Failed.\n");
}

//========================================================================
// ::radSoundhalVoiceCreate
//========================================================================


IRadSoundHalVoice * radSoundHalVoiceCreate( radMemoryAllocator allocator )
{
    return new ( "radSoundHalVoiceWin", allocator ) radSoundHalVoiceWin( );
}




