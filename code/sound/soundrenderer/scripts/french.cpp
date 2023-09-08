#include <sound/soundrenderer/soundrenderingmanager.h>

namespace Sound {

void daSoundRenderingManager::RunFrenchSoundScripts( void )
{
    SetCurrentNameSpace( GetSoundNamespace() );
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_INGAME );
	#include "interactive_propsfr.inl"
    GetSoundManager()->GetSoundLoader()->SetCurrentCluster( SC_NEVER_LOADED );
    #include "dialogfr.inl"
    #include "nisfr.inl"
}

}
