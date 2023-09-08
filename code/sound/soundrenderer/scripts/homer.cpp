#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

void daSoundRenderingManager::RunHomerSoundScripts( void )
{
    SetCurrentNameSpace( GetCharacterNamespace( SC_CHAR_HOMER ) );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_CHAR_HOMER );
    #include "homer.inl"
}

}
