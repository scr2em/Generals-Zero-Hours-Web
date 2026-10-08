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

// FILE: WebGameEngine.cpp ////////////////////////////////////////////////////
//
// The game engine of the WebAssembly build, see WebGameEngine.h.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Common/WebGameEngine.h"
#include "WebDevice/Platform/WebPlatform.h"

#include "Common/PerfTimer.h"

#include "GameNetwork/LANAPICallbacks.h"

#include <windows.h>

extern DWORD TheMessageTime;

//-------------------------------------------------------------------------------------------------
WebGameEngine::WebGameEngine()
{
}

//-------------------------------------------------------------------------------------------------
WebGameEngine::~WebGameEngine()
{
}

//-------------------------------------------------------------------------------------------------
void WebGameEngine::init()
{

	// extending functionality
	GameEngine::init();

}

//-------------------------------------------------------------------------------------------------
void WebGameEngine::reset()
{

	// extending functionality
	GameEngine::reset();

}

//-------------------------------------------------------------------------------------------------
void WebGameEngine::update()
{

	// call the engine normal update
	GameEngine::update();

	// A hidden tab or a window without focus is still a running game (the browser
	// throttles us anyway), unlike a minimized window on Windows which stops the
	// frame loop, so there is no equivalent of the "iconic" wait loop here.

	// let the browser's input reach the game
	serviceWindowsOS();

}

//-------------------------------------------------------------------------------------------------
/** Drains the platform's message queue into the window procedure, exactly like the
	* Win32 engine does with the system queue. */
//-------------------------------------------------------------------------------------------------
void WebGameEngine::serviceWindowsOS()
{
	WebPlatformMsg msg;

	while( WebPlatform_PeekMessage( &msg, 1 ) )
	{

		if( msg.message == WEBWM_QUIT )
		{
			// the page asked us to stop
			setQuitting( TRUE );
			break;
		}

		TheMessageTime = msg.time;
		WebPlatform_DispatchMessage( &msg );
		TheMessageTime = 0;

	}

}
