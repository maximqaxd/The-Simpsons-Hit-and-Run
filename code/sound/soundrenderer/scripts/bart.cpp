#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

void daSoundRenderingManager::RunBartSoundScripts( void )
{
    SetCurrentNameSpace( GetCharacterNamespace( SC_CHAR_BART ) );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_CHAR_BART );
    #include "bart.inl"
}

}
