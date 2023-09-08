#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

void daSoundRenderingManager::RunApuSoundScripts( void )
{
    SetCurrentNameSpace( GetCharacterNamespace( SC_CHAR_APU ) );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_CHAR_APU );
    #include "apu.inl"
}

}
