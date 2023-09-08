#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

void daSoundRenderingManager::RunGermanSoundScripts( void )
{
    SetCurrentNameSpace( GetSoundNamespace() );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_INGAME );
	#include "interactive_propsge.inl"
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_NEVER_LOADED );
    #include "dialogge.inl"
    #include "nisge.inl"
}

}
