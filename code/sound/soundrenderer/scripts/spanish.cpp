#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

void daSoundRenderingManager::RunSpanishSoundScripts( void )
{
    SetCurrentNameSpace( GetSoundNamespace() );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_INGAME );
	#include "interactive_propssp.inl"
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_NEVER_LOADED );
    #include "dialogsp.inl"
    #include "nissp.inl"
}

}
