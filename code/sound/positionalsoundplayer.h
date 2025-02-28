//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        positionalsoundplayer.h
//
// Description: Declaration of wrapper class for playing positional sounds
//
// History:     12/18/2002 + Created -- Darren
//
//=============================================================================

#ifndef POSITIONALSOUNDPLAYER_H
#define POSITIONALSOUNDPLAYER_H

//========================================
// Nested Includes
//========================================
#ifndef RAD_DREAMCAST
#include <radsoundmath.hpp>
#endif
#include <sound/simpsonssoundplayer.h>

//========================================
// Forward References
//========================================
class positionalSoundSettings;
struct IRadSoundHalPositionalGroup;

//=============================================================================
//
// Synopsis:    PositionCarrier
//
//=============================================================================

class PositionCarrier
{
    public:
        PositionCarrier();
        virtual ~PositionCarrier();
#ifndef RAD_DREAMCAST
        virtual void GetPosition( radSoundVector& position ) = 0;
        virtual void GetVelocity( radSoundVector& velocity ) = 0;
#endif

    protected:
    private:
        //Prevent wasteful constructor creation.
        PositionCarrier( const PositionCarrier& positioncarrier );
        PositionCarrier& operator=( const PositionCarrier& positioncarrier );
};

//=============================================================================
//
// Synopsis:    PositionalSoundPlayer
//
//=============================================================================

class PositionalSoundPlayer : public SimpsonsSoundPlayer
{
    public:
        PositionalSoundPlayer( );
        virtual ~PositionalSoundPlayer();

        bool PlayResource( IDaSoundResource* resource,
                           SimpsonsSoundPlayerCallback* callback = NULL );
#ifndef RAD_DREAMCAST
        void PlayQueuedSound( radSoundVector& position,
                              SimpsonsSoundPlayerCallback* callback = NULL );
#endif
        void SetPositionCarrier( PositionCarrier& movingSound );
        void UnsetPositionCarrier();

        void SetParameters( positionalSoundSettings* settings );
        positionalSoundSettings* GetParameters() { return( m_positionalSettings ); }

        void ServiceOncePerFrame();

        void SetPosition( float x, float y, float z );

    protected:
        //
        // Called when we're done with the sound renderer player object
        //
        void dumpSoundPlayer();

    private:
        //Prevent wasteful constructor creation.
        PositionalSoundPlayer( const PositionalSoundPlayer& positionalsoundplayer );
        PositionalSoundPlayer& operator=( const PositionalSoundPlayer& positionalsoundplayer );

        //
        // Pointer to sound source, used only if source is moving
        //
        PositionCarrier* m_positionCarrier;

        positionalSoundSettings* m_positionalSettings;

        float m_minDist;
        float m_maxDist;
#ifndef RAD_DREAMCAST
        radSoundVector m_position;
#endif
        bool m_outOfRange;
};

//*****************************************************************************
//
//Inline Public Member Functions
//
//*****************************************************************************

#endif //POSITIONALSOUNDPLAYER_H
