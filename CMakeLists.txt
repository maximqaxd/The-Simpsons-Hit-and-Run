cmake_minimum_required(VERSION 3.25)

project(SRR2)

if(VITA)
	include("${VITASDK}/share/vita.cmake" REQUIRED)

	add_compile_definitions(RAD_VITA)
	## Configuration options for this app
	# Display name (under bubble in LiveArea)
	set(VITA_APP_NAME "Simpsons Hit and Run")
	# Unique ID must be exactly 9 characters. Recommended: XXXXYYYYY where X =
	# unique string of developer and Y = a unique number for this app
	set(VITA_TITLEID  "ROADRAGE2")
	# Optional version string to show in LiveArea's more info screen
	set(VITA_VERSION  "00.60")
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
endif()

find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(SRR2_OPENAL_IMPL "OpenAL-Soft" CACHE STRING "OpenAL implementation")
set_property(CACHE SRR2_OPENAL_IMPL PROPERTY STRINGS "OpenAL-Soft" "MojoAL")

if (SRR2_OPENAL_IMPL STREQUAL "OpenAL-Soft")
	find_package(OpenAL REQUIRED)
	set(SRR2_OPENAL_TARGET OpenAL::OpenAL)
else()
	add_subdirectory(libs/sdks/mojoal SYSTEM)
	set(SRR2_OPENAL_TARGET mojoal)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	add_compile_options(-fvisibility=hidden)
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_options(
			-Werror
			-ggdb
		)
	endif()
	if(VITA)
		add_compile_options(
			-mtune=cortex-a9
			-mfpu=neon
		)
	endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_compile_definitions(RAD_DEBUG)
else()
	add_compile_definitions(RAD_RELEASE)
endif()

add_compile_definitions(
	RAD_CONSOLE
	RAD_WIN32
)

set(SRR2_FILENAME "Simpsons")
set(SRR2_REGION "NTSC")
option(SRR2_PAL OFF)
if(SRR2_PAL)
	set(SRR2_REGION "PAL")
	add_compile_definitions(PAL)
endif()

option(SRR2_USE_DLMALLOC OFF)
if(SRR2_USE_DLMALLOC)
	add_compile_definitions(USE_DOUG_LEA_HEAP)
endif()

option(SRR2_REROUTE_ALL_HEAP_ALLOCATIONS OFF)
if(SRR2_REROUTE_ALL_HEAP_ALLOCATIONS)
	add_compile_definitions(REROUTE_ALL_HEAP_ALLOCATIONS)
endif()

option(SRR2_USE_BINK OFF)
if(SRR2_USE_BINK)
	add_compile_definitions(RAD_MOVIEPLAYER_USE_BINK)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(FFmpeg REQUIRED IMPORTED_TARGET
			libavformat
			libavcodec
			libavutil
			libswresample
			libswscale
	)
endif()

set(SRR2_P3D_PDDI "OpenGL" CACHE STRING "Pure3D PDDI implementation")
set_property(CACHE SRR2_P3D_PDDI PROPERTY STRINGS "OpenGL" "GLES1" "GLES2")
if(SRR2_P3D_PDDI STREQUAL "GLES1")
	add_compile_definitions(RAD_GLES)
	add_compile_definitions(RAD_GLES_VERSION=1)
endif()
if(SRR2_P3D_PDDI STREQUAL "GLES2")
	add_compile_definitions(RAD_GLES)
	add_compile_definitions(RAD_GLES_VERSION=2)
endif()
if(VITA)
	option(SRR2_USE_VITAGL ON)
	if(SRR2_USE_VITAGL)
		add_compile_definitions(RAD_VITAGL)
	endif()
endif()

if(WIN32)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

option(SRR2_BUILD_TESTS ON)

set(SRR2_SOURCES
	"code/ai/actionbuttonhandler.cpp"
	"code/ai/actionbuttonmanager.cpp"
	"code/ai/actor/ActorAnimationUFO.cpp"
	"code/ai/actor/actoranimationwasp.cpp"
	"code/ai/actor/actor.cpp"
	"code/ai/actor/actordsg.cpp"
	"code/ai/actor/actormanager.cpp"
	"code/ai/actor/attackbehaviour.cpp"
	"code/ai/actor/attractionbehaviour.cpp"
	"code/ai/actor/cutcambehaviour.cpp"
	"code/ai/actor/evasionbehaviour.cpp"
	"code/ai/actor/flyingactor.cpp"
	"code/ai/actor/intersectionlist.cpp"
	"code/ai/actor/projectile.cpp"
	"code/ai/actor/projectiledsg.cpp"
	"code/ai/actor/spawnpoint.cpp"
	"code/ai/actor/stunnedbehaviour.cpp"
	"code/ai/actor/ufoattackbehaviour.cpp"
	"code/ai/actor/ufobeamalwaysonbehaviour.cpp"
	"code/ai/actor/ufobehaviour.cpp"
	"code/ai/automaticdoor.cpp"
	"code/ai/playanimonce.cpp"
	"code/ai/sequencer/actioncontroller.cpp"
	"code/ai/sequencer/action.cpp"
	"code/ai/sequencer/sequencer.cpp"
	"code/ai/sequencer/task.cpp"
	"code/ai/state.cpp"
	"code/ai/statemanager.cpp"
	"code/ai/vehicle/chaseai.cpp"
	"code/ai/vehicle/potentialfield.cpp"
	"code/ai/vehicle/potentials.cpp"
	"code/ai/vehicle/trafficai.cpp"
	"code/ai/vehicle/vehicleai.cpp"
	"code/ai/vehicle/vehicleairender.cpp"
	"code/ai/vehicle/waypointai.cpp"
	"code/atc/atcloader.cpp"
	"code/atc/atcmanager.cpp"
	"code/camera/animatedcam.cpp"
	"code/camera/bumpercam.cpp"
	"code/camera/chasecam.cpp"
	"code/camera/conversationcam.cpp"
	"code/camera/debugcam.cpp"
	"code/camera/firstpersoncam.cpp"
	"code/camera/followcam.cpp"
	"code/camera/kullcam.cpp"
	#"code/camera/pccam.cpp"
	"code/camera/railcam.cpp"
	"code/camera/relativeanimatedcam.cpp"
	"code/camera/reversecam.cpp"
	"code/camera/sinecosshaker.cpp"
	"code/camera/snapshotcam.cpp"
	"code/camera/staticcam.cpp"
	"code/camera/supercamcentral.cpp"
	"code/camera/supercamcontroller.cpp"
	"code/camera/supercam.cpp"
	"code/camera/supercammanager.cpp"
	"code/camera/supersprintcam.cpp"
	"code/camera/surveillancecam.cpp"
	"code/camera/trackercam.cpp"
	"code/camera/walkercam.cpp"
	"code/camera/wrecklesscam.cpp"
	"code/camera/wrecklesseventlistener.cpp"
	"code/cards/bonuscard.cpp"
	"code/cards/card.cpp"
	"code/cards/cardgallery.cpp"
	"code/cards/cardsdb.cpp"
	"code/cards/collectorcard.cpp"
	"code/cheats/cheatinputhandler.cpp"
	"code/cheats/cheatinputsystem.cpp"
	"code/cheats/cheats.cpp"
	"code/console/console.cpp"
	"code/console/debugconsolecallback.cpp"
	"code/console/fbstricmp.cpp"
	"code/console/nameinsensitive.cpp"
	"code/console/upcase.cpp"
	"code/contexts/bootupcontext.cpp"
	"code/contexts/context.cpp"
	"code/contexts/demo/democontext.cpp"
	"code/contexts/demo/loadingdemocontext.cpp"
	"code/contexts/entrycontext.cpp"
	"code/contexts/exitcontext.cpp"
	"code/contexts/frontendcontext.cpp"
	"code/contexts/gameplay/gameplaycontext.cpp"
	"code/contexts/gameplay/loadinggameplaycontext.cpp"
	"code/contexts/loadingcontext.cpp"
	"code/contexts/pausecontext.cpp"
	"code/contexts/playingcontext.cpp"
	"code/contexts/supersprint/loadingsupersprintcontext.cpp"
	"code/contexts/supersprint/supersprintcontext.cpp"
	"code/contexts/supersprint/supersprintfecontext.cpp"
	"code/data/gamedatamanager.cpp"
	"code/data/memcard/memorycardmanager.cpp"
	"code/data/PersistentWorldManager.cpp"
	"code/data/savegameinfo.cpp"
	"code/debug/debuginfo.cpp"
	"code/debug/profiler.cpp"
	"code/debug/section.cpp"
	"code/events/eventlistener.cpp"
	"code/events/eventmanager.cpp"
	"code/gameflow/gameflow.cpp"
	#"code/input/basedamper.cpp"
	"code/input/button.cpp"
	#"code/input/constanteffect.cpp"
	#"code/input/FEMouse.cpp"
	#"code/input/forceeffect.cpp"
	#"code/input/Gamepad.cpp"
	"code/input/inputmanager.cpp"
	#"code/input/Keyboard.cpp"
	"code/input/mappable.cpp"
	"code/input/mapper.cpp"
	#"code/input/Mouse.cpp"
	"code/input/MouseCursor.cpp"
	"code/input/RealController.cpp"
	"code/input/rumbleeffect.cpp"
	"code/input/rumblewin32.cpp"
	#"code/input/steeringspring.cpp"
	"code/input/usercontroller.cpp"
	#"code/input/usercontrollerWin32.cpp"
	#"code/input/virtualinputs.cpp"
	#"code/input/wheelrumble.cpp"
	"code/interiors/interiormanager.cpp"
	"code/loading/cameradataloader.cpp"
	"code/loading/cementfilehandler.cpp"
	"code/loading/choreofilehandler.cpp"
	"code/loading/consolefilehandler.cpp"
	"code/loading/filehandlerfactory.cpp"
	"code/loading/iconfilehandler.cpp"
	"code/loading/intersectionloader.cpp"
	"code/loading/loadingmanager.cpp"
	"code/loading/locatorloader.cpp"
	"code/loading/p3dfilehandler.cpp"
	"code/loading/pathloader.cpp"
	"code/loading/roaddatasegmentloader.cpp"
	"code/loading/roadloader.cpp"
	"code/loading/scroobyfilehandler.cpp"
	"code/loading/soundfilehandler.cpp"
	"code/main/commandlineoptions.cpp"
	"code/main/game.cpp"
	"code/main/pchsrr2.cpp"
	"code/main/singletons.cpp"
	"code/main/tuidunaligned.cpp"
	"code/main/win32main.cpp"
	"code/main/win32platform.cpp"
	"code/memory/classsizetracker.cpp"
	"code/memory/createheap.cpp"
	"code/memory/leakdetection.cpp"
	"code/memory/memorypool.cpp"
	"code/memory/memoryutilities.cpp"
	"code/memory/propstats.cpp"
	"code/memory/srrmemory.cpp"
	"code/meta/actioneventlocator.cpp"
	"code/meta/carstartlocator.cpp"
	"code/meta/directionallocator.cpp"
	"code/meta/eventlocator.cpp"
	"code/meta/fovlocator.cpp"
	"code/meta/interiorentrancelocator.cpp"
	"code/meta/locator.cpp"
	"code/meta/occlusionlocator.cpp"
	"code/meta/recttriggervolume.cpp"
	"code/meta/scriptlocator.cpp"
	"code/meta/spheretriggervolume.cpp"
	"code/meta/splinelocator.cpp"
	"code/meta/staticcamlocator.cpp"
	"code/meta/triggerlocator.cpp"
	"code/meta/triggervolume.cpp"
	"code/meta/triggervolumetracker.cpp"
	"code/meta/zoneeventlocator.cpp"
	"code/mission/animatedicon.cpp"
	"code/mission/bonusmissioninfo.cpp"
	"code/mission/charactersheet/charactersheetmanager.cpp"
	"code/mission/conditions/damagecondition.cpp"
	"code/mission/conditions/followcondition.cpp"
	"code/mission/conditions/getoutofcarcondition.cpp"
	"code/mission/conditions/keepbarrelcondition.cpp"
	"code/mission/conditions/leaveinteriorcondition.cpp"
	"code/mission/conditions/missioncondition.cpp"
	"code/mission/conditions/notabductedcondition.cpp"
	"code/mission/conditions/outofboundscondition.cpp"
	"code/mission/conditions/positioncondition.cpp"
	"code/mission/conditions/racecondition.cpp"
	"code/mission/conditions/timeoutcondition.cpp"
	"code/mission/conditions/vehiclecarryingstateprop.cpp"
	"code/mission/conditions/vehiclecondition.cpp"
	"code/mission/gameplaymanager.cpp"
	"code/mission/haspresentationinfo.cpp"
	"code/mission/mission.cpp"
	"code/mission/missionmanager.cpp"
	"code/mission/missionscriptloader.cpp"
	"code/mission/missionstage.cpp"
	"code/mission/nocopbonusobjective.cpp"
	"code/mission/nodamagebonusobjective.cpp"
	"code/mission/objectives/buycarobjective.cpp"
	"code/mission/objectives/buyskinobjective.cpp"
	"code/mission/objectives/coinobjective.cpp"
	"code/mission/objectives/collectdumpedobjective.cpp"
	"code/mission/objectives/collectibleobjective.cpp"
	"code/mission/objectives/deliveryobjective.cpp"
	"code/mission/objectives/destroybossobjective.cpp"
	"code/mission/objectives/destroyobjective.cpp"
	"code/mission/objectives/dialogueobjective.cpp"
	"code/mission/objectives/fmvobjective.cpp"
	"code/mission/objectives/followobjective.cpp"
	"code/mission/objectives/getinobjective.cpp"
	"code/mission/objectives/gooutsideobjective.cpp"
	"code/mission/objectives/gotoobjective.cpp"
	"code/mission/objectives/interiorobjective.cpp"
	"code/mission/objectives/loadvehicleobjective.cpp"
	"code/mission/objectives/loseobjective.cpp"
	"code/mission/objectives/missionobjective.cpp"
	"code/mission/objectives/pickupitemobjective.cpp"
	"code/mission/objectives/raceobjective.cpp"
	"code/mission/objectives/talktoobjective.cpp"
	"code/mission/objectives/timerobjective.cpp"
	"code/mission/racepositionbonusobjective.cpp"
	"code/mission/respawnmanager/respawnentity.cpp"
	"code/mission/respawnmanager/respawnmanager.cpp"
	"code/mission/rewards/merchandise.cpp"
	"code/mission/rewards/reward.cpp"
	"code/mission/rewards/rewardsmanager.cpp"
	"code/mission/safezone/safezone.cpp"
	"code/mission/statepropcollectible.cpp"
	"code/mission/timeremainbonusobjective.cpp"
	"code/mission/ufo/tractorbeam.cpp"
	"code/mission/ufo/ufo.cpp"
	"code/pedpaths/path.cpp"
	"code/pedpaths/pathmanager.cpp"
	"code/pedpaths/pathsegment.cpp"
	"code/presentation/animplayer.cpp"
	"code/presentation/blinker.cpp"
	"code/presentation/cameraplayer.cpp"
	"code/presentation/fmvplayer/fmvplayer.cpp"
	"code/presentation/fmvplayer/fmvuserinputhandler.cpp"
	"code/presentation/gui/backend/guimanagerbackend.cpp"
	"code/presentation/gui/backend/guiscreendemo.cpp"
	"code/presentation/gui/backend/guiscreenloading.cpp"
	"code/presentation/gui/backend/guiscreenloadingfe.cpp"
	"code/presentation/gui/bootup/guimanagerbootup.cpp"
	"code/presentation/gui/bootup/guimanagerlanguage.cpp"
	"code/presentation/gui/bootup/guiscreenbootupload.cpp"
	"code/presentation/gui/bootup/guiscreenlanguage.cpp"
	"code/presentation/gui/bootup/guiscreenlicense.cpp"
	"code/presentation/gui/frontend/guimanagerfrontend.cpp"
	"code/presentation/gui/frontend/guiscreencardgallery.cpp"
	"code/presentation/gui/frontend/guiscreencontroller.cpp"
	#"code/presentation/gui/frontend/guiscreencontrollerWin32.cpp"
	#"code/presentation/gui/frontend/guiscreendisplay.cpp"
	"code/presentation/gui/frontend/guiscreenloadgame.cpp"
	"code/presentation/gui/frontend/guiscreenmainmenu.cpp"
	"code/presentation/gui/frontend/guiscreenmissiongallery.cpp"
	#"code/presentation/gui/frontend/guiscreenmultichoosechar.cpp"
	#"code/presentation/gui/frontend/guiscreenmultisetup.cpp"
	"code/presentation/gui/frontend/guiscreenoptions.cpp"
	"code/presentation/gui/frontend/guiscreenplaymovie.cpp"
	"code/presentation/gui/frontend/guiscreenscrapbookcontents.cpp"
	"code/presentation/gui/frontend/guiscreenscrapbook.cpp"
	"code/presentation/gui/frontend/guiscreenscrapbookstats.cpp"
	"code/presentation/gui/frontend/guiscreenskingallery.cpp"
	"code/presentation/gui/frontend/guiscreensound.cpp"
	"code/presentation/gui/frontend/guiscreensplash.cpp"
	"code/presentation/gui/frontend/guiscreenvehiclegallery.cpp"
	"code/presentation/gui/frontend/guiscreenviewcredits.cpp"
	"code/presentation/gui/frontend/guiscreenviewmovies.cpp"
	"code/presentation/gui/guientity.cpp"
	"code/presentation/gui/guimanager.cpp"
	"code/presentation/gui/guimenu.cpp"
	"code/presentation/gui/guimenuitem.cpp"
	"code/presentation/gui/guiscreen.cpp"
	"code/presentation/gui/guiscreenmemcardcheck.cpp"
	"code/presentation/gui/guiscreenmemorycard.cpp"
	"code/presentation/gui/guiscreenmessage.cpp"
	"code/presentation/gui/guiscreenprompt.cpp"
	"code/presentation/gui/guisystem.cpp"
	"code/presentation/gui/guitextbible.cpp"
	"code/presentation/gui/guiuserinputhandler.cpp"
	"code/presentation/gui/guiwindow.cpp"
	"code/presentation/gui/ingame/guimanageringame.cpp"
	"code/presentation/gui/ingame/guiscreencreditspostfmv.cpp"
	"code/presentation/gui/ingame/guiscreenhastransitions.cpp"
	"code/presentation/gui/ingame/guiscreenhud.cpp"
	#"code/presentation/gui/ingame/guiscreenhudmap.cpp"
	"code/presentation/gui/ingame/guiscreeniriswipe.cpp"
	"code/presentation/gui/ingame/guiscreenletterbox.cpp"
	"code/presentation/gui/ingame/guiscreenlevelend.cpp"
	"code/presentation/gui/ingame/guiscreenlevelstats.cpp"
	"code/presentation/gui/ingame/guiscreenmissionbase.cpp"
	"code/presentation/gui/ingame/guiscreenmissionload.cpp"
	"code/presentation/gui/ingame/guiscreenmissionover.cpp"
	"code/presentation/gui/ingame/guiscreenmissionselect.cpp"
	"code/presentation/gui/ingame/guiscreenmissionsuccess.cpp"
	"code/presentation/gui/ingame/guiscreenmultihud.cpp"
	"code/presentation/gui/ingame/guiscreenpausecontroller.cpp"
	"code/presentation/gui/ingame/guiscreenpause.cpp"
	#"code/presentation/gui/ingame/guiscreenpausedisplay.cpp"
	"code/presentation/gui/ingame/guiscreenpausemission.cpp"
	"code/presentation/gui/ingame/guiscreenpauseoptions.cpp"
	"code/presentation/gui/ingame/guiscreenpausesettings.cpp"
	"code/presentation/gui/ingame/guiscreenpausesound.cpp"
	"code/presentation/gui/ingame/guiscreenpausesunday.cpp"
	"code/presentation/gui/ingame/guiscreenphonebooth.cpp"
	"code/presentation/gui/ingame/guiscreenpurchaserewards.cpp"
	"code/presentation/gui/ingame/guiscreenrewards.cpp"
	"code/presentation/gui/ingame/guiscreensavegame.cpp"
	"code/presentation/gui/ingame/guiscreentutorial.cpp"
	"code/presentation/gui/ingame/guiscreenviewcards.cpp"
	"code/presentation/gui/ingame/hudevents/hudcardcollected.cpp"
	"code/presentation/gui/ingame/hudevents/hudcoincollected.cpp"
	"code/presentation/gui/ingame/hudevents/hudcountdown.cpp"
	"code/presentation/gui/ingame/hudevents/hudeventhandler.cpp"
	"code/presentation/gui/ingame/hudevents/hudhitnrun.cpp"
	"code/presentation/gui/ingame/hudevents/huditemdropped.cpp"
	"code/presentation/gui/ingame/hudevents/hudmissionobjective.cpp"
	"code/presentation/gui/ingame/hudevents/hudmissionprogress.cpp"
	"code/presentation/gui/ingame/hudevents/hudwaspdestroyed.cpp"
	"code/presentation/gui/minigame/guimanagerminigame.cpp"
	"code/presentation/gui/minigame/guiscreenminihud.cpp"
	"code/presentation/gui/minigame/guiscreenminimenu.cpp"
	"code/presentation/gui/minigame/guiscreenminipause.cpp"
	"code/presentation/gui/minigame/guiscreenminisummary.cpp"
	"code/presentation/gui/utility/hudmapcam.cpp"
	"code/presentation/gui/utility/hudmap.cpp"
	"code/presentation/gui/utility/numerictext.cpp"
	"code/presentation/gui/utility/scrollingtext.cpp"
	"code/presentation/gui/utility/slider.cpp"
	"code/presentation/gui/utility/specialfx.cpp"
	"code/presentation/gui/utility/teletypetext.cpp"
	"code/presentation/gui/utility/transitions.cpp"
	"code/presentation/language.cpp"
	"code/presentation/mouthflapper.cpp"
	"code/presentation/nisplayer.cpp"
	"code/presentation/playerdrawable.cpp"
	"code/presentation/presentationanimator.cpp"
	"code/presentation/presentation.cpp"
	"code/presentation/presevents/fmvevent.cpp"
	"code/presentation/presevents/nisevent.cpp"
	"code/presentation/presevents/presentationevent.cpp"
	"code/presentation/presevents/transevent.cpp"
	"code/presentation/simpleanimationplayer.cpp"
	"code/presentation/transitionplayer.cpp"
	"code/presentation/tutorialmanager.cpp"
	"code/render/animentitydsgmanager/animentitydsgmanager.cpp"
	"code/render/breakables/breakablesmanager.cpp"
	"code/render/Culling/BoxPts.cpp"
	"code/render/Culling/CellBlock.cpp"
	"code/render/Culling/Cell.cpp"
	"code/render/Culling/CoordSubList.cpp"
	"code/render/Culling/CullData.cpp"
	"code/render/Culling/HexahedronP.cpp"
	"code/render/Culling/ISpatialProxy.cpp"
	"code/render/Culling/OctTreeData.cpp"
	"code/render/Culling/OctTreeNode.cpp"
	"code/render/Culling/SpatialTree.cpp"
	"code/render/Culling/SpatialTreeIter.cpp"
	"code/render/Culling/SphereSP.cpp"
	"code/render/Culling/VectorLib.cpp"
	"code/render/Culling/WorldScene.cpp"
	"code/render/DSG/animcollisionentitydsg.cpp"
	"code/render/DSG/animentitydsg.cpp"
	"code/render/DSG/breakableobjectdsg.cpp"
	"code/render/DSG/collisionentitydsg.cpp"
	"code/render/DSG/DSGFactory.cpp"
	"code/render/DSG/DynaPhysDSG.cpp"
	"code/render/DSG/FenceEntityDSG.cpp"
	"code/render/DSG/IEntityDSG.cpp"
	"code/render/DSG/InstAnimDynaPhysDSG.cpp"
	"code/render/DSG/InstDynaPhysDSG.cpp"
	"code/render/DSG/InstStatEntityDSG.cpp"
	"code/render/DSG/InstStatPhysDSG.cpp"
	"code/render/DSG/IntersectDSG.cpp"
	"code/render/DSG/LensFlareDSG.cpp"
	"code/render/DSG/StatePropDSG.cpp"
	"code/render/DSG/StaticEntityDSG.cpp"
	"code/render/DSG/StaticPhysDSG.cpp"
	"code/render/DSG/TriStripDSG.cpp"
	"code/render/DSG/WorldSphereDSG.cpp"
	"code/render/IntersectManager/IntersectManager.cpp"
	"code/render/Loaders/AllWrappers.cpp"
	"code/render/Loaders/AnimCollLoader.cpp"
	"code/render/Loaders/AnimDSGLoader.cpp"
	"code/render/Loaders/AnimDynaPhysLoader.cpp"
	"code/render/Loaders/BillboardWrappedLoader.cpp"
	"code/render/Loaders/breakableobjectloader.cpp"
	"code/render/Loaders/DynaPhysLoader.cpp"
	"code/render/Loaders/FenceLoader.cpp"
	"code/render/Loaders/GeometryWrappedLoader.cpp"
	"code/render/Loaders/instparticlesystemloader.cpp"
	"code/render/Loaders/InstStatEntityLoader.cpp"
	"code/render/Loaders/InstStatPhysLoader.cpp"
	"code/render/Loaders/IntersectLoader.cpp"
	"code/render/Loaders/LensFlareLoader.cpp"
	"code/render/Loaders/StaticEntityLoader.cpp"
	"code/render/Loaders/StaticPhysLoader.cpp"
	"code/render/Loaders/TreeDSGLoader.cpp"
	"code/render/Loaders/WorldSphereLoader.cpp"
	"code/render/Particles/particlemanager.cpp"
	"code/render/Particles/particlesystemdsg.cpp"
	"code/render/Particles/vehicleparticleemitter.cpp"
	"code/render/RenderFlow/renderflow.cpp"
	"code/render/RenderManager/FrontEndRenderLayer.cpp"
	"code/render/RenderManager/RenderLayer.cpp"
	"code/render/RenderManager/RenderManager.cpp"
	"code/render/RenderManager/WorldRenderLayer.cpp"
	"code/roads/geometry.cpp"
	"code/roads/intersection.cpp"
	"code/roads/lane.cpp"
	"code/roads/road.cpp"
	"code/roads/roadmanager.cpp"
	"code/roads/roadrender.cpp"
	"code/roads/roadrendertest.cpp"
	"code/roads/roadsegment.cpp"
	"code/roads/roadsegmentdata.cpp"
	"code/roads/trafficcontrol.cpp"
	"code/sound/avatar/avatarsoundplayer.cpp"
	"code/sound/avatar/carsoundparameters.cpp"
	"code/sound/avatar/soundavatar.cpp"
	"code/sound/avatar/vehiclesounddebugpage.cpp"
	"code/sound/avatar/vehiclesoundplayer.cpp"
	"code/sound/dialog/conversation.cpp"
	"code/sound/dialog/conversationmatcher.cpp"
	"code/sound/dialog/dialogcoordinator.cpp"
	"code/sound/dialog/dialogline.cpp"
	"code/sound/dialog/dialoglist.cpp"
	"code/sound/dialog/dialogpriorityqueue.cpp"
	"code/sound/dialog/dialogqueueelement.cpp"
	"code/sound/dialog/dialogselectiongroup.cpp"
	"code/sound/dialog/dialogsounddebugpage.cpp"
	"code/sound/dialog/playabledialog.cpp"
	"code/sound/dialog/selectabledialog.cpp"
	"code/sound/listener.cpp"
	"code/sound/movingpositional/actorplayer.cpp"
	"code/sound/movingpositional/aivehiclesoundplayer.cpp"
	"code/sound/movingpositional/animobjsoundplayer.cpp"
	"code/sound/movingpositional/avatarvehicleposnplayer.cpp"
	"code/sound/movingpositional/movingsoundmanager.cpp"
	"code/sound/movingpositional/platformsoundplayer.cpp"
	"code/sound/movingpositional/trafficsoundplayer.cpp"
	"code/sound/movingpositional/vehicleposnsoundplayer.cpp"
	"code/sound/movingpositional/waspsoundplayer.cpp"
	"code/sound/music/musicplayer.cpp"
	"code/sound/nis/nissoundplayer.cpp"
	"code/sound/positionalsoundplayer.cpp"
	"code/sound/simpsonssoundplayer.cpp"
	"code/sound/soundcluster.cpp"
	"code/sound/sounddebug/sounddebugdisplay.cpp"
	"code/sound/sounddebug/sounddebugpage.cpp"
	"code/sound/soundfx/positionalsoundsettings.cpp"
	"code/sound/soundfx/reverbcontroller.cpp"
	"code/sound/soundfx/reverbsettings.cpp"
	"code/sound/soundfx/soundeffectplayer.cpp"
	"code/sound/soundfx/soundfxfrontendlogic.cpp"
	"code/sound/soundfx/soundfxgameplaylogic.cpp"
	"code/sound/soundfx/soundfxlogic.cpp"
	"code/sound/soundfx/soundfxpauselogic.cpp"
	"code/sound/soundfx/win32reverbcontroller.cpp"
	"code/sound/soundloader.cpp"
	"code/sound/soundmanager.cpp"
	"code/sound/soundrenderercallback.cpp"
	"code/sound/soundrenderer/dasoundplayer.cpp"
	"code/sound/soundrenderer/fader.cpp"
	"code/sound/soundrenderer/musicsoundplayer.cpp"
	"code/sound/soundrenderer/playermanager.cpp"
	"code/sound/soundrenderer/scripts/apu.cpp"
	"code/sound/soundrenderer/scripts/bart.cpp"
	"code/sound/soundrenderer/scripts/cars.cpp"
	"code/sound/soundrenderer/scripts/effects.cpp"
	"code/sound/soundrenderer/scripts/english.cpp"
	"code/sound/soundrenderer/scripts/homer.cpp"
	"code/sound/soundrenderer/scripts/levels.cpp"
	"code/sound/soundrenderer/scripts/lisa.cpp"
	"code/sound/soundrenderer/scripts/marge.cpp"
	"code/sound/soundrenderer/scripts/tuning.cpp"
	"code/sound/soundrenderer/soundallocatedresource.cpp"
	"code/sound/soundrenderer/sounddynaload.cpp"
	"code/sound/soundrenderer/soundnucleus.cpp"
	"code/sound/soundrenderer/soundrenderingmanager.cpp"
	"code/sound/soundrenderer/soundresource.cpp"
	"code/sound/soundrenderer/soundresourcemanager.cpp"
	"code/sound/soundrenderer/soundtuner.cpp"
	"code/sound/soundrenderer/tunerdebugpage.cpp"
	"code/sound/soundrenderer/wireplayers.cpp"
	"code/sound/soundrenderer/wiresystem.cpp"
	"code/sound/tuning/globalsettings.cpp"
	"code/stateprop/stateprop.cpp"
	"code/stateprop/statepropdata.cpp"
	"code/supersprint/supersprintdata.cpp"
	"code/supersprint/supersprintmanager.cpp"
	"code/worldsim/avatar.cpp"
	"code/worldsim/avatarmanager.cpp"
	"code/worldsim/character/aicharactercontroller.cpp"
	"code/worldsim/character/charactercontroller.cpp"
	"code/worldsim/character/character.cpp"
	"code/worldsim/character/charactermanager.cpp"
	"code/worldsim/character/charactermappable.cpp"
	"code/worldsim/character/characterrenderable.cpp"
	"code/worldsim/character/charactertarget.cpp"
	"code/worldsim/character/footprint/footprintmanager.cpp"
	"code/worldsim/coins/coinmanager.cpp"
	"code/worldsim/coins/sparkle.cpp"
	"code/worldsim/groundplanepool.cpp"
	"code/worldsim/harass/chasemanager.cpp"
	"code/worldsim/hitnrunmanager.cpp"
	"code/worldsim/huskpool.cpp"
	"code/worldsim/parkedcars/parkedcarmanager.cpp"
	"code/worldsim/ped/pedestrian.cpp"
	"code/worldsim/ped/pedestrianmanager.cpp"
	"code/worldsim/redbrick/geometryvehicle.cpp"
	"code/worldsim/redbrick/physicslocomotioncontrollerforces.cpp"
	"code/worldsim/redbrick/physicslocomotion.cpp"
	"code/worldsim/redbrick/redbrickcollisionsolveragent.cpp"
	"code/worldsim/redbrick/rootmatrixdriver.cpp"
	"code/worldsim/redbrick/suspensionjointdriver.cpp"
	"code/worldsim/redbrick/trafficbodydrawable.cpp"
	"code/worldsim/redbrick/trafficlocomotion.cpp"
	"code/worldsim/redbrick/vehiclecontroller/aivehiclecontroller.cpp"
	"code/worldsim/redbrick/vehiclecontroller/humanvehiclecontroller.cpp"
	"code/worldsim/redbrick/vehiclecontroller/vehiclecontroller.cpp"
	"code/worldsim/redbrick/vehiclecontroller/vehiclemappable.cpp"
	"code/worldsim/redbrick/vehicle.cpp"
	"code/worldsim/redbrick/vehicleeventlistener.cpp"
	"code/worldsim/redbrick/vehicleinit.cpp"
	"code/worldsim/redbrick/vehiclelocomotion.cpp"
	"code/worldsim/redbrick/wheel.cpp"
	"code/worldsim/skidmarks/skidmark.cpp"
	"code/worldsim/skidmarks/SkidMarkGenerator.cpp"
	"code/worldsim/skidmarks/skidmarkmanager.cpp"
	"code/worldsim/spawn/spawnmanager.cpp"
	"code/worldsim/traffic/trafficmanager.cpp"
	"code/worldsim/vehiclecentral.cpp"
	"code/worldsim/worldcollisionsolveragent.cpp"
	"code/worldsim/worldobject.cpp"
	"code/worldsim/worldphysicsmanager.cpp"
)
if(SRR2_PAL)
	list(APPEND SRR2_SOURCES
		"code/sound/soundrenderer/scripts/french.cpp"
		"code/sound/soundrenderer/scripts/german.cpp"
		"code/sound/soundrenderer/scripts/spanish.cpp"
	)
endif()

set(RADCORE_SOURCES
	#"libs/radcore/src/pch/pch.cpp"
	#"libs/radcore/src/rad1394/win32lib/rad1394master.cpp"
	#"libs/radcore/src/radcontroller/directinputcontroller.cpp"
	"libs/radcore/src/radcontroller/sdlcontroller.cpp"
	#"libs/radcore/src/raddebugcommunication/host1394socket.cpp"
	#"libs/radcore/src/raddebugcommunication/hostchannel.cpp"
	#"libs/radcore/src/raddebugcommunication/host.cpp"
	#"libs/radcore/src/raddebugcommunication/hosthiosocket.cpp"
	#"libs/radcore/src/raddebugcommunication/targetconnection.cpp"
	#"libs/radcore/src/raddebugcommunication/targetsocketchannel.cpp"
	"libs/radcore/src/raddebugcommunication/targetx.cpp"
	"libs/radcore/src/raddebugconsole/consoleclient.cpp"
	"libs/radcore/src/raddebug/debug.cpp"
	#"libs/radcore/src/raddebugfileserver/fileserver.cpp"
	#"libs/radcore/src/raddebugwatch/watchclient.cpp"
	"libs/radcore/src/raddispatch/dispatcher.cpp"
	"libs/radcore/src/radfile/common/cementer.cpp"
	"libs/radcore/src/radfile/common/drive.cpp"
	"libs/radcore/src/radfile/common/drivethread.cpp"
	"libs/radcore/src/radfile/common/filecache.cpp"
	"libs/radcore/src/radfile/common/file.cpp"
	"libs/radcore/src/radfile/common/filesystem.cpp"
	"libs/radcore/src/radfile/common/instancedrive.cpp"
	"libs/radcore/src/radfile/common/platformdrives.cpp"
	"libs/radcore/src/radfile/common/radfile.cpp"
	"libs/radcore/src/radfile/common/remotedrive.cpp"
	"libs/radcore/src/radfile/common/requests.cpp"
	"libs/radcore/src/radfile/win32/win32drive.cpp"
	"libs/radcore/src/radkey/radkey.cpp"
	"libs/radcore/src/radmemory/align.cpp"
	"libs/radcore/src/radmemory/binallocator.cpp"
	"libs/radcore/src/radmemory/dlheap.cpp"
	"libs/radcore/src/radmemory/externalmemoryheap.cpp"
	"libs/radcore/src/radmemory/externalmemoryobject.cpp"
	"libs/radcore/src/radmemory/memorymanager.cpp"
	"libs/radcore/src/radmemory/memoryobject.cpp"
	"libs/radcore/src/radmemory/memorypool.cpp"
	"libs/radcore/src/radmemory/memoryspacewin32.cpp"
	"libs/radcore/src/radmemorymonitor/memmonitorclient.cpp"
	"libs/radcore/src/radmemorymonitor/memmonitorclienthelp.cpp"
	"libs/radcore/src/radmemory/platalloc.cpp"
	"libs/radcore/src/radmemory/staticheap.cpp"
	"libs/radcore/src/radmemory/trackingheap.cpp"
	"libs/radcore/src/radmemory/twowayallocator.cpp"
	"libs/radcore/src/radobjectbtree/objectavltree.cpp"
	"libs/radcore/src/radobjectbtree/objectbtree.cpp"
	"libs/radcore/src/radobjectlist/dynamicarray.cpp"
	"libs/radcore/src/radobjectlist/objectlist.cpp"
	"libs/radcore/src/radobjectlist/weakcallbackwrapper.cpp"
	"libs/radcore/src/radobjectlist/weakinterfacewrapper.cpp"
	"libs/radcore/src/radobject/object.cpp"
	"libs/radcore/src/radplatform/platform.cpp"
	"libs/radcore/src/radprofiler/profiler2.cpp"
	"libs/radcore/src/radprofiler/profiler.cpp"
	"libs/radcore/src/radprofiler/profilesample.cpp"
	"libs/radcore/src/radremotecommand/functionlist.cpp"
	"libs/radcore/src/radremotecommand/remotecommandserver.cpp"
	"libs/radcore/src/radremotecommand/remotecommandtarget.cpp"
	"libs/radcore/src/radstacktrace/win32/stacktrace.cpp"
	"libs/radcore/src/radstats/simplestat.cpp"
	"libs/radcore/src/radstats/statmanager.cpp"
	"libs/radcore/src/radstring/string.cpp"
	"libs/radcore/src/radtextdisplay/textdisplay.cpp"
	"libs/radcore/src/radthread/mutex.cpp"
	"libs/radcore/src/radthread/semaphore.cpp"
	"libs/radcore/src/radthread/system.cpp"
	"libs/radcore/src/radthread/thread.cpp"
	"libs/radcore/src/radtime/stopwatch.cpp"
	"libs/radcore/src/radtime/time.cpp"
)

option(SRR2_MICROPROFILE OFF)
if(SRR2_MICROPROFILE)
	add_compile_definitions(MICROPROFILE_ENABLED)
	list(APPEND RADCORE_SOURCES	"libs/radcore/src/radprofiler/microprofile.cpp")
endif()

set(RADCONTENT_SOURCES
	"libs/radcontent/src/radload/request.cpp"
	"libs/radcontent/src/radload/queue.cpp"
	"libs/radcontent/src/radload/hashtable.cpp"
	"libs/radcontent/src/radload/inventory.cpp"
	"libs/radcontent/src/radload/stream.cpp"
	"libs/radcontent/src/radload/manager.cpp"
	"libs/radcontent/src/radload/object.cpp"
	#"libs/radcontent/src/pch/pch.cpp"
)

set(P3D_SOURCES
	"libs/pure3d/p3d/ambientlight.cpp"
	"libs/pure3d/p3d/anim/animate.cpp"
	"libs/pure3d/p3d/anim/animatedobject.cpp"
	"libs/pure3d/p3d/anim/billboardobjectanimation.cpp"
	"libs/pure3d/p3d/anim/cameraanimation.cpp"
	"libs/pure3d/p3d/anim/channel.cpp"
	"libs/pure3d/p3d/anim/compositedrawable.cpp"
	"libs/pure3d/p3d/anim/drawablepose.cpp"
	"libs/pure3d/p3d/anim/eventanimation.cpp"
	"libs/pure3d/p3d/anim/event.cpp"
	"libs/pure3d/p3d/anim/expressionanimation.cpp"
	"libs/pure3d/p3d/anim/expression.cpp"
	"libs/pure3d/p3d/anim/expressionoffsets.cpp"
	"libs/pure3d/p3d/anim/instancedgeometry.cpp"
	"libs/pure3d/p3d/anim/lightanimation.cpp"
	"libs/pure3d/p3d/anim/multicontroller.cpp"
	"libs/pure3d/p3d/anim/polyskin.cpp"
	"libs/pure3d/p3d/anim/poseanimation.cpp"
	"libs/pure3d/p3d/anim/pose.cpp"
	"libs/pure3d/p3d/anim/sequencer.cpp"
	"libs/pure3d/p3d/anim/shaderanimation.cpp"
	"libs/pure3d/p3d/anim/skeleton.cpp"
	"libs/pure3d/p3d/anim/textureanimation.cpp"
	"libs/pure3d/p3d/anim/vertexanimcontroller.cpp"
	"libs/pure3d/p3d/anim/vertexanimkey.cpp"
	"libs/pure3d/p3d/anim/vertexanimobject.cpp"
	"libs/pure3d/p3d/anim/vertexoffsetexpression.cpp"
	"libs/pure3d/p3d/anim/visibilityanimation.cpp"
	"libs/pure3d/p3d/billboardobject.cpp"
	"libs/pure3d/p3d/bmp.cpp"
	"libs/pure3d/p3d/camera.cpp"
	"libs/pure3d/p3d/cameraloader.cpp"
	"libs/pure3d/p3d/chunkfile.cpp"
	"libs/pure3d/p3d/context.cpp"
	"libs/pure3d/p3d/debugdraw.cpp"
	"libs/pure3d/p3d/directionallight.cpp"
	"libs/pure3d/p3d/displaylist.cpp"
	"libs/pure3d/p3d/drawable.cpp"
	"libs/pure3d/p3d/dxtn.cpp"
	"libs/pure3d/p3d/effects/effect.cpp"
	"libs/pure3d/p3d/effects/optic.cpp"
	"libs/pure3d/p3d/effects/opticlensflare.cpp"
	"libs/pure3d/p3d/effects/opticloader.cpp"
	"libs/pure3d/p3d/effects/particlearray.cpp"
	"libs/pure3d/p3d/effects/particleemitter.cpp"
	"libs/pure3d/p3d/effects/particlegenerator.cpp"
	"libs/pure3d/p3d/effects/particleloader.cpp"
	"libs/pure3d/p3d/effects/particlepool.cpp"
	"libs/pure3d/p3d/effects/particlesystem.cpp"
	"libs/pure3d/p3d/effects/particletype.cpp"
	"libs/pure3d/p3d/effects/particleutility.cpp"
	"libs/pure3d/p3d/effects/transitionmanager.cpp"
	"libs/pure3d/p3d/entity.cpp"
	"libs/pure3d/p3d/error.cpp"
	"libs/pure3d/p3d/file.cpp"
	"libs/pure3d/p3d/fileftt.cpp"
	"libs/pure3d/p3d/font.cpp"
	"libs/pure3d/p3d/gameattr.cpp"
	"libs/pure3d/p3d/geometry.cpp"
	"libs/pure3d/p3d/imageconverter.cpp"
	"libs/pure3d/p3d/image.cpp"
	"libs/pure3d/p3d/imagefactory.cpp"
	"libs/pure3d/p3d/imagefont.cpp"
	"libs/pure3d/p3d/inventory.cpp"
	"libs/pure3d/p3d/light.cpp"
	"libs/pure3d/p3d/lightloader.cpp"
	"libs/pure3d/p3d/lightschooser.cpp"
	"libs/pure3d/p3d/loaders.cpp"
	"libs/pure3d/p3d/loadmanager.cpp"
	"libs/pure3d/p3d/locator.cpp"
	"libs/pure3d/p3d/lzr.cpp"
	"libs/pure3d/p3d/matrixstack.cpp"
	"libs/pure3d/p3d/memheap.cpp"
	"libs/pure3d/p3d/memory.cpp"
	"libs/pure3d/p3d/memorysection.cpp"
	#"libs/pure3d/p3d/platform/linux/plat_filemap.cpp"
	"libs/pure3d/p3d/platform/win32/platform.cpp"
	"libs/pure3d/p3d/png.cpp"
	"libs/pure3d/p3d/pointcamera.cpp"
	"libs/pure3d/p3d/pointlight.cpp"
	"libs/pure3d/p3d/primgroup.cpp"
	"libs/pure3d/p3d/rawimage.cpp"
	"libs/pure3d/p3d/refcounted.cpp"
	"libs/pure3d/p3d/scenegraph/nodeanimation.cpp"
	"libs/pure3d/p3d/scenegraph/scenegraph.cpp"
	"libs/pure3d/p3d/shader.cpp"
	"libs/pure3d/p3d/shadow/shadow_common.cpp"
	"libs/pure3d/p3d/shadow/shadow_gc.cpp"
	"libs/pure3d/p3d/shadow/shadow_generic.cpp"
	"libs/pure3d/p3d/shadow/shadow_ps2.cpp"
	"libs/pure3d/p3d/spotlight.cpp"
	"libs/pure3d/p3d/sprite.cpp"
	"libs/pure3d/p3d/table.cpp"
	"libs/pure3d/p3d/targa.cpp"
	"libs/pure3d/p3d/textdataparser.cpp"
	"libs/pure3d/p3d/textstring.cpp"
	"libs/pure3d/p3d/texture.cpp"
	"libs/pure3d/p3d/texturefont.cpp"
	"libs/pure3d/p3d/unicode.cpp"
	"libs/pure3d/p3d/utility.cpp"
	"libs/pure3d/p3d/vectorcamera.cpp"
	"libs/pure3d/p3d/vertexlist.cpp"
	"libs/pure3d/p3d/view.cpp"
)
if (WIN32)
	list(APPEND P3D_SOURCES "libs/pure3d/p3d/platform/win32/plat_filemap.cpp")
endif()

set(CHOREO_SOURCES
	"libs/choreo/src/animation.cpp"
	"libs/choreo/src/bank.cpp"
	"libs/choreo/src/basebank.cpp"
	"libs/choreo/src/blend.cpp"
	"libs/choreo/src/blendtemplate.cpp"
	"libs/choreo/src/constants.cpp"
	"libs/choreo/src/driver.cpp"
	"libs/choreo/src/engine.cpp"
	"libs/choreo/src/footblenddriver.cpp"
	"libs/choreo/src/footblender.cpp"
	"libs/choreo/src/foot.cpp"
	"libs/choreo/src/footplanter.cpp"
	"libs/choreo/src/internalanimation.cpp"
	"libs/choreo/src/jointblenddriver.cpp"
	"libs/choreo/src/jointblender.cpp"
	"libs/choreo/src/load.cpp"
	"libs/choreo/src/locomotion.cpp"
	"libs/choreo/src/partition.cpp"
	"libs/choreo/src/puppet.cpp"
	"libs/choreo/src/replayblenddriver.cpp"
	"libs/choreo/src/replayblender.cpp"
	"libs/choreo/src/replay.cpp"
	"libs/choreo/src/rig.cpp"
	"libs/choreo/src/rootblenddriver.cpp"
	"libs/choreo/src/rootblender.cpp"
	"libs/choreo/src/root.cpp"
	"libs/choreo/src/scriptreader.cpp"
	"libs/choreo/src/scriptwriter.cpp"
	"libs/choreo/src/synchronization.cpp"
	"libs/choreo/src/transition.cpp"
	"libs/choreo/src/utility.cpp"
)

set(POSER_SOURCES
	"libs/poser/src/joint.cpp"
	"libs/poser/src/pose.cpp"
	"libs/poser/src/posedriver.cpp"
	"libs/poser/src/poseengine.cpp"
	"libs/poser/src/transform.cpp"
)

set(RADMATH_SOURCES
	"libs/radmath/radmath/matrix.cpp"
	"libs/radmath/radmath/numerical.cpp"
	"libs/radmath/radmath/plane.cpp"
	"libs/radmath/radmath/quaternion.cpp"
	"libs/radmath/radmath/radmath.cpp"
	"libs/radmath/radmath/random.cpp"
	"libs/radmath/radmath/spline.cpp"
	"libs/radmath/radmath/triangle.cpp"
	"libs/radmath/radmath/trig.cpp"
	"libs/radmath/radmath/vector.cpp"
)
if(VITA)
	list(APPEND RADMATH_SOURCES
		"libs/radmath/radmath/platform/vita/neonmatrix.cpp"
		"libs/radmath/radmath/platform/vita/neonvector.cpp"
	)
endif()

if(SRR2_USE_BINK)
	set(RADMOVIE_SOURCES
		"libs/radmovie/src/common/binkmovieplayer.cpp"
		"libs/radmovie/src/common/binkradfile.cpp"
		"libs/radmovie/src/common/binkrenderstrategy.cpp"
		"libs/radmovie/src/pch/pch.cpp"
	)
else()
	set(RADMOVIE_SOURCES
		"libs/radmovie/src/common/ffmpegmovieplayer.cpp"
		"libs/radmovie/src/common/binkrenderstrategy.cpp"
		"libs/radmovie/src/pch/pch.cpp"
	)
endif()

set(RADMUSIC_SOURCES
	"libs/radmusic/src/memory/memory.cpp"
	"libs/radmusic/src/ods/ods_codegen.cpp"
	"libs/radmusic/src/ods/ods.cpp"
	"libs/radmusic/src/ods/ods_memory.cpp"
	"libs/radmusic/src/ods/ods_parser.cpp"
	"libs/radmusic/src/ods/ods_util.cpp"
	"libs/radmusic/src/pch/pch.cpp"
	"libs/radmusic/src/radmusic/framework/framework.cpp"
	"libs/radmusic/src/radmusic/music/music_composition.cpp"
	"libs/radmusic/src/radmusic/music/music_engine.cpp"
	"libs/radmusic/src/radmusic/music/music_engine_state_fade.cpp"
	"libs/radmusic/src/radmusic/music/music_engine_state_steady.cpp"
	"libs/radmusic/src/radmusic/music/music_engine_state_stitch.cpp"
	"libs/radmusic/src/radmusic/music/music_engine_state_stopped.cpp"
	"libs/radmusic/src/radmusic/music/music_performance.cpp"
	"libs/radmusic/src/radmusic/resource/resource_manager.cpp"
	"libs/radmusic/src/radmusic/schema/schema_util.cpp"
	"libs/radmusic/src/radmusic/sequence/sequence_player.cpp"
	"libs/radmusic/src/radmusic/sequence/sequence_region.cpp"
	"libs/radmusic/src/radmusic/sequence/sequence_stream_graph.cpp"
)

set(RADSOUND_SOURCES
	"libs/radsound/src/common/radsoundobject.cpp"
	"libs/radsound/src/common/radsoundupdatableobject.cpp"
	"libs/radsound/src/hal/common/audioformat.cpp"
	"libs/radsound/src/hal/common/banner.cpp"
	"libs/radsound/src/hal/common/memoryregion.cpp"
	"libs/radsound/src/hal/common/radsoundfile.cpp"
	"libs/radsound/src/hal/common/rolloff.cpp"
	"libs/radsound/src/hal/win32/buffer.cpp"
	"libs/radsound/src/hal/win32/bufferloader.cpp"
	"libs/radsound/src/hal/win32/effect.cpp"
	"libs/radsound/src/hal/win32/listener.cpp"
	"libs/radsound/src/hal/win32/positionalgroup.cpp"
	"libs/radsound/src/hal/win32/radsoundwin.cpp"
	"libs/radsound/src/hal/win32/system.cpp"
	"libs/radsound/src/hal/win32/voice.cpp"
	"libs/radsound/src/math/radsoundmath.cpp"
	"libs/radsound/src/pch/pch.cpp"
	"libs/radsound/src/radsound/buffereddatasource.cpp"
	"libs/radsound/src/radsound/clip.cpp"
	"libs/radsound/src/radsound/clipplayer.cpp"
	"libs/radsound/src/radsound/datacache.cpp"
	"libs/radsound/src/radsound/memorydatasource.cpp"
	"libs/radsound/src/radsound/memoryspaceobject.cpp"
	"libs/radsound/src/radsound/radicaladpcm.cpp"
	"libs/radsound/src/radsound/rsdfiledatasource.cpp"
	"libs/radsound/src/radsound/stitcheddatasource.cpp"
	"libs/radsound/src/radsound/streamplayer.cpp"
)

set(RADSCRIPT_SOURCES
	"libs/radscript/src/factory/radfactory.cpp"
	"libs/radscript/src/namespace/namespace.cpp"
	"libs/radscript/src/pch/pch.cpp"
	"libs/radscript/src/script/remotescript.cpp"
	"libs/radscript/src/script/script.cpp"
	"libs/radscript/src/typeinfo/TypeInfoEnum.cpp"
	"libs/radscript/src/typeinfo/TypeInfoInstance.cpp"
	"libs/radscript/src/typeinfo/TypeInfoInterface.cpp"
	"libs/radscript/src/typeinfo/TypeInfoIntLiteral.cpp"
	"libs/radscript/src/typeinfo/TypeInfoLoader.cpp"
	"libs/radscript/src/typeinfo/TypeInfoMethod.cpp"
	"libs/radscript/src/typeinfo/TypeInfoParam.cpp"
	"libs/radscript/src/typeinfo/TypeInfoSystem.cpp"
	"libs/radscript/src/typeinfoutil/typeinfodistributor.cpp"
	"libs/radscript/src/typeinfoutil/typeinfoutil.cpp"
	"libs/radscript/src/typeinfo/win32/win32typeinfovfcall.cpp"
)

set(SCROOBY_SOURCES
	"libs/scrooby/src/FeApp.cpp"
	"libs/scrooby/src/FeBoundedDrawable.cpp"
	"libs/scrooby/src/FeChunkHandler.cpp"
	"libs/scrooby/src/FeDrawable.cpp"
	"libs/scrooby/src/FeEntity.cpp"
	"libs/scrooby/src/FeGroup.cpp"
	"libs/scrooby/src/FeLanguage.cpp"
	"libs/scrooby/src/FeLayer.cpp"
	"libs/scrooby/src/FeLoaders.cpp"
	"libs/scrooby/src/FeMovie.cpp"
	"libs/scrooby/src/FeOwner.cpp"
	"libs/scrooby/src/FePage.cpp"
	"libs/scrooby/src/FeParent.cpp"
	"libs/scrooby/src/FePolygon.cpp"
	"libs/scrooby/src/FeProject.cpp"
	"libs/scrooby/src/FePure3dObject.cpp"
	"libs/scrooby/src/FeScreen.cpp"
	"libs/scrooby/src/FeSprite.cpp"
	"libs/scrooby/src/FeTextBible.cpp"
	"libs/scrooby/src/FeText.cpp"
	"libs/scrooby/src/FeTextStyle.cpp"
	"libs/scrooby/src/ResourceManager/FeResourceManager.cpp"
	"libs/scrooby/src/scrooby/App.cpp"
	"libs/scrooby/src/strings/pcstring.cpp"
	"libs/scrooby/src/strings/stricmp.cpp"
	"libs/scrooby/src/strings/UnicodeString.cpp"
	"libs/scrooby/src/tLinearTable.cpp"
	"libs/scrooby/src/utility/AsyncFileLoader.cpp"
	"libs/scrooby/src/utility/BufferReader.cpp"
	"libs/scrooby/src/utility/debugMessages.cpp"
	"libs/scrooby/src/utility/EnumConversion.cpp"
	"libs/scrooby/src/xml/XMLParser.cpp"
	"libs/scrooby/src/xml/XMLTree.cpp"
)

set(SIM_SOURCES
	"libs/sim/simcollision/collisionanalyser.cpp"
	"libs/sim/simcollision/collisionanalyserdata.cpp"
	"libs/sim/simcollision/collisionanalyserdataUID.cpp"
	"libs/sim/simcollision/collisionanalyserevent.cpp"
	"libs/sim/simcollision/collisionanalyserinfo.cpp"
	"libs/sim/simcollision/collisionanalysertranslator.cpp"
	"libs/sim/simcollision/collision.cpp"
	"libs/sim/simcollision/collisiondetector.cpp"
	"libs/sim/simcollision/collisiondisplay.cpp"
	"libs/sim/simcollision/collisionmanager.cpp"
	"libs/sim/simcollision/collisionobject.cpp"
	"libs/sim/simcollision/collisionvolume.cpp"
	"libs/sim/simcollision/impulsebasedcollisionsolver.cpp"
	"libs/sim/simcollision/proximitydetection.cpp"
	"libs/sim/simcollision/subcollisiondetector.cpp"
	"libs/sim/simcommon/arraymath.cpp"
	"libs/sim/simcommon/dline2.cpp"
	"libs/sim/simcommon/impulselink.cpp"
	"libs/sim/simcommon/kalmann.cpp"
	"libs/sim/simcommon/largesymmetricmatrix.cpp"
	"libs/sim/simcommon/physicsproperties.cpp"
	"libs/sim/simcommon/sbmatrix.cpp"
	"libs/sim/simcommon/simconstraint.cpp"
	"libs/sim/simcommon/simenvironment.cpp"
	"libs/sim/simcommon/simmath.cpp"
	"libs/sim/simcommon/simstatearticulated.cpp"
	"libs/sim/simcommon/simstate.cpp"
	"libs/sim/simcommon/simstateflexible.cpp"
	"libs/sim/simcommon/simstatetarget.cpp"
	"libs/sim/simcommon/simtarget.cpp"
	"libs/sim/simcommon/simulatedobject.cpp"
	"libs/sim/simcommon/simutility.cpp"
	"libs/sim/simcommon/skeletoninfo.cpp"
	"libs/sim/simcommon/symmatrix.cpp"
	"libs/sim/simcommon/tlist.cpp"
	"libs/sim/simcommon/trackerjointmodifier.cpp"
	"libs/sim/simcommon/trajectoryextrapolator.cpp"
	"libs/sim/simphysics/articulatedphysicsobject.cpp"
	"libs/sim/simphysics/physicsjoint.cpp"
	"libs/sim/simphysics/physicsobject.cpp"
	"libs/sim/simphysics/restingdetector.cpp"
	"libs/sim/simphysics/virtualcm.cpp"
)

if(SRR2_USE_BINK)
	set(BINK_SOURCES
		"libs/sdks/bink/binkacd.cpp"
		"libs/sdks/bink/binkasyncRR.c"
		"libs/sdks/bink/binkasyncthread.c"
		"libs/sdks/bink/binkcomm.c"
		"libs/sdks/bink/binkfile.c"
		"libs/sdks/bink/binkmarkers.c"
		"libs/sdks/bink/binkread.c"
		"libs/sdks/bink/binkoal.c"
		"libs/sdks/bink/binkssmix.c"
		"libs/sdks/bink/binkutil.c"
		"libs/sdks/bink/blitter.c" # custom
		"libs/sdks/bink/expand2.c"
		"libs/sdks/bink/popmal.c"
		"libs/sdks/bink/radfft.cpp"
	)

	if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
		list(APPEND BINK_SOURCES "libs/sdks/bink/x86_cpu.c")
	endif ()

	if (WIN32)
		list(APPEND BINK_SOURCES
			"libs/sdks/bink/binkplugin.c"
			"libs/sdks/bink/binkpluginw32.c"
			"libs/sdks/bink/binkxa2.cpp"
			"libs/sdks/bink/win32_radmem.c"
			"libs/sdks/bink/win32_rrAtomics.cpp"
			"libs/sdks/bink/win32_rrCpu.c"
			"libs/sdks/bink/win32_rrThreads2.c"
			"libs/sdks/bink/wintimer.c"
		)
		#if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
		#	list(APPEND BINK_SOURCES "libs/sdks/bink/win64_rrAtomics_x64.asm")
		#endif()
	else()
		list(APPEND BINK_SOURCES
			"libs/sdks/bink/binklnx.c"
			#"libs/sdks/bink/binkpa.c"
			#"libs/sdks/bink/binkplugin.c"
			#"libs/sdks/bink/binkpluginlnx.c"
			"libs/sdks/bink/binkssmix.c"
			"libs/sdks/bink/posix_defread.c"
			"libs/sdks/bink/posix_radmem.c"
			"libs/sdks/bink/posix_rrCpu.c"
			"libs/sdks/bink/posix_rrThreads2.c"
			"libs/sdks/bink/cpp_rratomics.cpp"
		)
	endif()
endif()

set(PDDI_SOURCES
	"libs/pure3d/pddi/base/basecontext.cpp"
	"libs/pure3d/pddi/base/baseshader.cpp"
	"libs/pure3d/pddi/base/font.cpp"
	"libs/pure3d/pddi/base/pddiobj.cpp"
)
if(SRR2_P3D_PDDI STREQUAL "GLES2")
	list(APPEND PDDI_SOURCES
		"libs/pure3d/pddi/gles/glcon.cpp"
		"libs/pure3d/pddi/gles/gldev.cpp"
		"libs/pure3d/pddi/gles/glmat.cpp"
		"libs/pure3d/pddi/gles/gltex.cpp"
		"libs/pure3d/pddi/gles/glprog.cpp"
		"libs/pure3d/pddi/gles/glad/src/glad.c"
	)
	if(SRR2_USE_VITAGL)
		list(APPEND PDDI_SOURCES "libs/pure3d/pddi/gles/display_vita/gldisplay.cpp")
	else()
		list(APPEND PDDI_SOURCES
			"libs/pure3d/pddi/gles/decompress.cpp"
			"libs/pure3d/pddi/gles/display_win32/gldisplay.cpp"
		)
	endif()
else()
	list(APPEND PDDI_SOURCES
		"libs/pure3d/pddi/gl/glcon.cpp"
		"libs/pure3d/pddi/gl/gldev.cpp"
		"libs/pure3d/pddi/gl/glmat.cpp"
		"libs/pure3d/pddi/gl/gltex.cpp"
		"libs/pure3d/pddi/gl/glad/src/glad.c"
	)
	if(SRR2_P3D_PDDI STREQUAL "GLES1")
		list(APPEND PDDI_SOURCES "libs/pure3d/pddi/gl/decompress.cpp")
	endif()
	if(VITA)
		list(APPEND PDDI_SOURCES "libs/pure3d/pddi/gl/display_vita/gldisplay.cpp")
	else()
		list(APPEND PDDI_SOURCES "libs/pure3d/pddi/gl/display_win32/gldisplay.cpp")
	endif()
endif()

add_library(radcore STATIC ${RADCORE_SOURCES})
target_include_directories(radcore
	PUBLIC "libs/radcore/inc/"
	PRIVATE "libs/radcore/src/pch/" # TODO
	PUBLIC "code/" # wtf (radobject.hpp includes memory/classsizetracker.h)
)
target_compile_features(radcore PUBLIC cxx_std_17)
target_link_libraries(radcore SDL2::SDL2)

add_library(radcontent STATIC ${RADCONTENT_SOURCES})
target_include_directories(radcontent
	PUBLIC "libs/radcontent/inc/"
	#PRIVATE "code/" # wtf
	PRIVATE "libs/radcontent/src/"
)
target_link_libraries(radcontent
	radcore
	radmath
	pddi
)

add_library(pddi STATIC ${PDDI_SOURCES})
target_include_directories(pddi PUBLIC "libs/pure3d/")
if(SRR2_P3D_PDDI STREQUAL GLES2)
	target_include_directories(pddi PRIVATE "libs/pure3d/pddi/gles/glad/include/")
else()
	target_include_directories(pddi PRIVATE "libs/pure3d/pddi/gl/glad/include/")
endif()
target_link_libraries(pddi
	SDL2::SDL2
	radmath
	radcore
)

add_library(p3d STATIC ${P3D_SOURCES})
target_include_directories(p3d
	PUBLIC "libs/pure3d/"
	#PRIVATE "code/" # wtf
	PRIVATE "libs/" # wtf
)
target_link_libraries(p3d
	PNG::PNG
	pddi
	radcore
	radcontent
	radmath
)

add_library(choreo STATIC ${CHOREO_SOURCES})
target_include_directories(choreo
	PUBLIC "libs/choreo/inc/"
)
target_link_libraries(choreo
	p3d
	poser
	radcore
	sim
)

add_library(poser STATIC ${POSER_SOURCES})
target_include_directories(poser
	PUBLIC "libs/poser/inc/"
)
target_link_libraries(poser
	radcore
	p3d
	radmath
)

add_library(radmath STATIC ${RADMATH_SOURCES})
target_include_directories(radmath
	PUBLIC "libs/radmath/"
)
if(VITA)
	target_link_libraries(radmath mathneon)
endif()

add_library(radmovie STATIC ${RADMOVIE_SOURCES})
target_include_directories(radmovie
	PUBLIC "libs/radmovie/inc/"
	PRIVATE "libs/radmovie/src/pch/"
)
target_link_libraries(radmovie
	p3d
	radcore
	radsound
)
if(SRR2_USE_BINK)
	target_link_libraries(radmovie bink)
else()
	target_link_libraries(radmovie PkgConfig::FFmpeg)
endif()
if(SRR2_BUILD_TESTS)
	add_executable(simplemovie "libs/radmovie/sample/src/main.cpp" "libs/radmovie/sample/src/util.cpp"
		"code/main/tuidunaligned.cpp")
	target_link_libraries(simplemovie
		radmovie
		SDL2::SDL2
		p3d
		pddi
		radcore
		radsound
		PkgConfig::FFmpeg
	)
	if(VITA)
		if(SRR2_USE_VITAGL)
			target_link_libraries(simplemovie
				vitaGL
				vitashark
			)
		endif()
		target_link_libraries(simplemovie
			SceLibKernel_stub
			ScePvf_stub
			mathneon
			SceAudio_stub
			SceAppMgr_stub
			SceAppUtil_stub
			ScePgf_stub
			SceShaccCgExt
			SceShaccCg_stub
			SceCtrl_stub
			taihen_stub
			SceKernelDmacMgr_stub
		)
	endif()
	file(COPY "libs/radmovie/sample/bin/binkcd/sample_bink.rmv"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
	if(VITA)
		vita_create_self(simplemovie.self simplemovie)
	endif()
endif()

add_library(radmusic STATIC ${RADMUSIC_SOURCES})
target_include_directories(radmusic
	PUBLIC "libs/radmusic/inc/"
	PRIVATE "libs/radmusic/src/pch/"
	PRIVATE "libs/radmusic/src/"
)
target_link_libraries(radmusic
	radcore
	radcontent
	radsound
)

add_library(radsound STATIC ${RADSOUND_SOURCES})
target_include_directories(radsound
	PUBLIC "libs/radsound/inc/"
	PRIVATE "libs/radsound/src/pch/"
	PRIVATE "libs/radsound/src/common/"
)
target_link_libraries(radsound
	${SRR2_OPENAL_TARGET}
	radcore
)
if(SRR2_BUILD_TESTS)
	add_executable(simplesound "libs/radsound/sample/simplesound/src/main.cpp")
	target_link_libraries(simplesound
		radsound
		Threads::Threads
	)
	file(COPY "libs/radsound/sample/simplesound/bin/win/sample.rsd"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
	file(COPY "libs/radsound/sample/simplesound/bin/win/stream.rsd"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
	if(VITA)
		vita_create_self(simplesound.self simplesound)
	endif()
endif()

add_library(radscript STATIC ${RADSCRIPT_SOURCES})
target_include_directories(radscript
	PUBLIC "libs/radscript/inc/"
	PRIVATE "libs/radscript/src/pch/"
)
target_link_libraries(radscript
	radcore
)

add_library(scrooby STATIC ${SCROOBY_SOURCES})
target_include_directories(scrooby
	PUBLIC "libs/scrooby/inc/"
	PUBLIC "libs/scrooby/src/"
)
target_link_libraries(scrooby
	p3d
)

add_library(sim STATIC ${SIM_SOURCES})
target_include_directories(sim
	PUBLIC "libs/sim/"
)
target_link_libraries(sim
	p3d
	poser
)

if(SRR2_USE_BINK)
	add_library(bink STATIC ${BINK_SOURCES})
	target_compile_definitions(bink
		PRIVATE
			USING_EGT
			INC_BINK2
			NO_BINK20
			NO_BINK10_SUPPORT
		PUBLIC
			__RADFINAL__
			__RADINSTATICLIB__
	)
	target_link_libraries(bink ${SRR2_OPENAL_TARGET})
	if (WIN32)
		target_link_libraries(bink Winmm)
	endif()
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_compile_options(bink
			PRIVATE -Wno-multichar
		)
	endif()
	target_include_directories(bink
		PUBLIC "libs/sdks/bink/"
	)
endif()

add_executable(SRR2 ${SRR2_SOURCES})
target_link_libraries(SRR2
	SDL2::SDL2
	SDL2::SDL2main
	choreo
	p3d
	pddi
	poser
	radcore
	radcontent
	radmath
	radmovie
	radmusic
	radsound
	radscript
	scrooby
	sim
	Threads::Threads
)
if(SRR2_MICROPROFILE AND WIN32)
	# needed for microprofile
	target_link_libraries(SRR2 wsock32)
endif()
if(VITA)
	if(SRR2_USE_VITAGL)
		target_link_libraries(SRR2
			vitaGL
			vitashark
		)
	endif()
	target_link_libraries(SRR2
		SceLibKernel_stub
		ScePvf_stub
		mathneon
		SceAudio_stub
		SceAppMgr_stub
		SceAppUtil_stub
		ScePgf_stub
		SceShaccCgExt
		SceShaccCg_stub
		SceCtrl_stub
		taihen_stub
		SceKernelDmacMgr_stub
	)
endif()
if(SRR2_USE_BINK)
	target_link_libraries(SRR2 bink)
else()
	target_link_libraries(SRR2 PkgConfig::FFmpeg)
endif()
target_compile_features(SRR2 PRIVATE cxx_std_17)

option(SRR2_USE_PCH ON)
if (SRR2_USE_PCH)
	target_precompile_headers(SRR2 PRIVATE "code/main/pchsrr2.h")
endif()

target_include_directories(SRR2 PRIVATE "code/")

if(VITA)
	## Create Vita files
	if(NOT SRR2_USE_VITAGL)
		vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
		# The FILE directive lets you add additional files to the VPK, the syntax is
		# FILE src_path dst_path_in_vpk. In this case, we add the LiveArea paths.
		vita_create_vpk(${SRR2_FILENAME}${SRR2_REGION}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
			VERSION ${VITA_VERSION}
			NAME ${VITA_APP_NAME}
			FILE sce_sys/icon0.png sce_sys/icon0.png
			sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
			sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
			sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
			art art

			#suprx
			module/libGLESv2.suprx module/libGLESv2.suprx
			module/libgpu_es4_ext.suprx module/libgpu_es4_ext.suprx
			module/libIMGEGL.suprx module/libIMGEGL.suprx
			module/libpvrPSP2_WSEGL.suprx module/libpvrPSP2_WSEGL.suprx
		)
	else()
		vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
		# The FILE directive lets you add additional files to the VPK, the syntax is
		# FILE src_path dst_path_in_vpk. In this case, we add the LiveArea paths.
		vita_create_vpk(${SRR2_FILENAME}${SRR2_REGION}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
			VERSION ${VITA_VERSION}
			NAME ${VITA_APP_NAME}
			FILE sce_sys/icon0.png sce_sys/icon0.png
			sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
			sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
			sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
			art art
		)
	endif()
endif()
