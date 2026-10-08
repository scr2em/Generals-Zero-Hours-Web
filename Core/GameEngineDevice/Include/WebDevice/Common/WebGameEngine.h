/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: WebGameEngine.h //////////////////////////////////////////////////////
//
// The game engine of the WebAssembly build. It is the Win32 game engine with
// the Win32 device specific parts replaced: the portable (std::filesystem) file
// systems on top of the WasmFS tree, the browser keyboard (W3DGameClient picks
// WebKeyboard and NullVideoPlayer when built for the web), a silent audio
// manager and no video playback. Rendering is unchanged (W3D on Direct3D 8,
// which the web build implements on WebGL2).
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameEngine.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/NetworkInterface.h"
#include "StdDevice/Common/StdBIGFileSystem.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "WebDevice/Common/WebNullAudioManager.h"
#include "W3DDevice/GameClient/W3DGameClient.h"

class WebGameEngine : public GameEngine
{

public:

	WebGameEngine();
	virtual ~WebGameEngine() override;

	virtual void init() override;												///< initialization
	virtual void reset() override;											///< reset engine
	virtual void update() override;											///< update the game engine
	virtual void serviceWindowsOS() override;						///< deliver the browser's input as window messages

protected:

	virtual GameLogic *createGameLogic() override;
	virtual GameClient *createGameClient() override;
	virtual ModuleFactory *createModuleFactory() override;
	virtual ThingFactory *createThingFactory() override;
	virtual FunctionLexicon *createFunctionLexicon() override;
	virtual LocalFileSystem *createLocalFileSystem() override;
	virtual ArchiveFileSystem *createArchiveFileSystem() override;
	virtual Radar *createRadar(Bool dummy) override;
	virtual WebBrowser *createWebBrowser() override;
	virtual AudioManager *createAudioManager(Bool dummy) override;
	virtual ParticleSystemManager *createParticleSystemManager(Bool dummy) override;

};

// INLINE -----------------------------------------------------------------------------------------
inline GameLogic *WebGameEngine::createGameLogic() { return NEW W3DGameLogic; }
inline GameClient *WebGameEngine::createGameClient() { return NEW W3DGameClient; }
inline ModuleFactory *WebGameEngine::createModuleFactory() { return NEW W3DModuleFactory; }
inline ThingFactory *WebGameEngine::createThingFactory() { return NEW W3DThingFactory; }
inline FunctionLexicon *WebGameEngine::createFunctionLexicon() { return NEW W3DFunctionLexicon; }
inline LocalFileSystem *WebGameEngine::createLocalFileSystem() { return NEW StdLocalFileSystem; }
inline ArchiveFileSystem *WebGameEngine::createArchiveFileSystem() { return NEW StdBIGFileSystem; }
inline WebBrowser *WebGameEngine::createWebBrowser() { return nullptr; }	// no embedded browser

inline ParticleSystemManager *WebGameEngine::createParticleSystemManager(Bool dummy)
{
	if (dummy)
		return NEW ParticleSystemManagerDummy;
	return NEW W3DParticleSystemManager;
}

inline Radar *WebGameEngine::createRadar(Bool dummy)
{
	if (dummy)
		return NEW RadarDummy;
	return NEW W3DRadar;
}

inline AudioManager *WebGameEngine::createAudioManager(Bool dummy)
{
	// The silent manager serves headless mode as well.
	return NEW WebNullAudioManager;
}
