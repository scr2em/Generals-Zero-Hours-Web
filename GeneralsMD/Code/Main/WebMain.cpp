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

// FILE: WebMain.cpp //////////////////////////////////////////////////////////
//
// Entry point of the WebAssembly build, the counterpart of WinMain.cpp.
//
// main() runs on a worker thread (-sPROXY_TO_PTHREAD). The engine's main loop
// cannot block there, though: the browser only shows a frame once the thread
// returns to its event loop, so the loop is run one frame per
// requestAnimationFrame (emscripten_set_main_loop) and main() returns right
// after starting it. The shutdown that follows GameMain() on Windows runs from
// the loop when the game quits. There is no window to create: the page provides a
// canvas, WebPlatform turns its input into the same window messages Windows
// would deliver, and WebWndProc below is the game's window procedure.
//
// Data: the page copied the player's game files into the Origin Private File
// System before starting us; WebPlatform_MountStorage() makes them appear as
// /game (Zero Hour), /generals (Generals) and /userdata (saves, options).
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "WinMain.h"
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/FramePacer.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "Common/GameSounds.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/MessageStream.h"
#include "Common/PlayerList.h"
#include "Common/ReplaySimulation.h"
#include "Common/Registry.h"
#include "Common/Team.h"
#include "Common/WorkingDirectory.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/Mouse.h"
#include "GameClient/Keyboard.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "WebDevice/Common/WebGameEngine.h"
#include "WebDevice/Platform/WebPlatform.h"
#include <WebD3D8/WebD3D8.h>
#include "Common/version.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"

// The command line the game parses, see CommandLine.cpp. WebCompat defines them.
extern int __argc;
extern char **__argv;

// GLOBALS ////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;  ///< our application instance
HWND ApplicationHWnd = nullptr;  ///< our application window handle: the canvas
Win32Mouse *TheWin32Mouse = nullptr;  ///< for the WndProc() only
DWORD TheMessageTime = 0;	///< For getting the time that a message was posted from the browser.

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
const char *gAppPrefix = ""; /// So WB can have a different debug log file name.

static Bool isWinMainActive = false;

// Where WebPlatform_MountStorage() puts the player's files.
static const char *const GAME_DIRECTORY = "/game";

// WebWndProc =================================================================
/** The window procedure: what WndProc in WinMain.cpp does, minus everything that
	* only matters for a real window (painting, system menu, power events, IME). */
//=============================================================================
static intptr_t WebWndProc( uintptr_t hWnd, uint32_t message, uintptr_t wParam, intptr_t lParam )
{

	try
	{

		// handle all window messages
		switch( message )
		{

			// ------------------------------------------------------------------------
			case WEBWM_CLOSE:
				// TheSuperHackers @feature Same as Alt+F4 on Windows: show the quit menu in-game,
				// or quit right away when not in a match.
				if (TheGameEngine && !TheGameEngine->getQuitting())
				{
					if (TheMessageStream && TheMessageStream->isReadyForMessages())
					{
						TheMessageStream->appendMessage(GameMessage::MSG_META_DEMO_INSTANT_QUIT);
					}
					else
					{
						TheGameEngine->setQuitting(TRUE);
					}
				}
				return 0;

			//-------------------------------------------------------------------------
			case WEBWM_SIZE:
			{
				if (TheMouse)
					TheMouse->refreshCursorCapture();

				break;
			}

			// ------------------------------------------------------------------------
			case WEBWM_SETFOCUS:
			{
				//
				// reset the state of our keyboard cause we haven't been paying
				// attention to the keys while focus was away
				//
				if (TheKeyboard)
					TheKeyboard->resetKeys();

				if (TheMouse)
					TheMouse->regainFocus();

				break;
			}

			//-------------------------------------------------------------------------
			case WEBWM_KILLFOCUS:
			{
				if (TheKeyboard)
					TheKeyboard->resetKeys();

				if (TheMouse)
				{
					TheMouse->loseFocus();

					if (TheMouse->isCursorInside())
					{
						TheMouse->onCursorMovedOutside();
					}
				}

				break;
			}

			//-------------------------------------------------------------------------
			case WEBWM_ACTIVATEAPP:
			{
				if ((bool) wParam != isWinMainActive)
				{
					isWinMainActive = (BOOL) wParam;

					if (TheGameEngine)
						TheGameEngine->setIsActive(isWinMainActive);

					if (isWinMainActive)
					{
						//restore mouse cursor to our custom version.
						if (TheWin32Mouse)
							TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
					}
				}
				return 0;
			}

			//-------------------------------------------------------------------------
			case WEBWM_ACTIVATE:
			{
				Int active = LOWORD( wParam );

				if( active == 0 /* WA_INACTIVE */ )
				{
					if (TheAudio)
						TheAudio->muteAudio(AudioManager::MuteAudioReason_WindowFocus);
				}
				else
				{
					if (TheAudio)
						TheAudio->unmuteAudio(AudioManager::MuteAudioReason_WindowFocus);

					// Cursor can only be captured after one of the activation events.
					if (TheMouse)
						TheMouse->refreshCursorCapture();
				}
				break;
			}

			//-------------------------------------------------------------------------
			case WEBWM_KEYDOWN:
			case WEBWM_KEYUP:
			case WEBWM_SYSKEYDOWN:
			case WEBWM_SYSKEYUP:
			case WEBWM_CHAR:
			{
				// The keyboard device reads the key transitions from the platform
				// layer; there is nothing to do here. (The Win32 version quits the
				// message loop on Escape, which the engine ignores anyway.)
				return 0;
			}

			//-------------------------------------------------------------------------
			case WEBWM_LBUTTONDOWN:
			case WEBWM_LBUTTONUP:
			case WEBWM_LBUTTONDBLCLK:

			case WEBWM_MBUTTONDOWN:
			case WEBWM_MBUTTONUP:
			case WEBWM_MBUTTONDBLCLK:

			case WEBWM_RBUTTONDOWN:
			case WEBWM_RBUTTONUP:
			case WEBWM_RBUTTONDBLCLK:
			{
				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;
			}

			//-------------------------------------------------------------------------
			case WEBWM_MOUSEWHEEL:
			{
				if( TheWin32Mouse == nullptr )
					return 0;

				long x = (short) LOWORD(lParam);
				long y = (short) HIWORD(lParam);
				int width, height;
				WebPlatform_GetClientSize( &width, &height );

				// ignore when outside of client area
				if( x < 0 || x > width || y < 0 || y > height )
					return 0;

				TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );
				return 0;
			}

			//-------------------------------------------------------------------------
			case WEBWM_MOUSEMOVE:
			{
				if( TheWin32Mouse == nullptr )
					return 0;

				// ignore when window is not active
				if( !isWinMainActive )
					return 0;

				Int x = (Int)LOWORD( lParam );
				Int y = (Int)HIWORD( lParam );
				int width, height;
				WebPlatform_GetClientSize( &width, &height );

				// ignore when outside of client area (positions left of or above it wrap
				// around to large unsigned values)
				if( x > width || y > height )
				{
					if ( TheMouse->isCursorInside() )
					{
						TheMouse->onCursorMovedOutside();
					}
					return 0;
				}

				if( !TheMouse->isCursorInside() )
				{
					TheMouse->onCursorMovedInside();
				}

				TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );
				return 0;
			}

		}

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in Main::WebWndProc... probably should not happen"));
		// no rethrow
	}

	return 0;

}

// Necessary to allow memory managers and such to have useful critical sections
static CriticalSection critSec1, critSec2, critSec3, critSec4, critSec5;

// shutdownApplication ========================================================
/** Everything that follows the end of the game: the counterpart of the code
	* after GameMain() in WinMain. Runs at the end of main() or, when the game
	* runs frame by frame, from the last frame. */
//=============================================================================
static void shutdownApplication( Int exitcode )
{
	try {

		delete TheVersion;
		TheVersion = nullptr;

	#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
	#endif
	#if defined(RTS_DEBUG)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
	#endif

		shutdownMemoryManager();
	}
	catch (...)
	{

	}

	WebPlatform_Shutdown();
	WebPlatform_NotifyExit( exitcode );

	TheUnicodeStringCriticalSection = nullptr;
	TheDmaCriticalSection = nullptr;
	TheMemoryPoolCriticalSection = nullptr;
}

// finishGame =================================================================
/** The end of GameMain(): the engine is done, free it. */
//=============================================================================
static void finishGame()
{
	delete TheFramePacer;
	TheFramePacer = nullptr;
	delete TheGameEngine;
	TheGameEngine = nullptr;
}

// reportFatal ================================================================
/** Debug and release crashes (RELEASE_CRASH, failed asserts) end up here instead of
	* in the Windows message box. The text goes to stderr, which the page collects
	* into its error panel; the game exits right after (ReleaseCrash ends with _exit). */
//=============================================================================
static void reportFatal( const char *message )
{
	// Debug builds route failed asserts here too; they carry on, release crashes end the game.
	const bool assertion = message && strncmp( message, "ASSERTION FAILURE", 17 ) == 0;
	fprintf( stderr, "%s: %s\n", assertion ? "Assertion failed" : "Fatal error", message ? message : "(no message)" );
	fflush( stderr );
}

// Frames run since the game started, for the page (window.__zhFrames, about twice a second)
// and for the log when -webframelog is on the command line.
static unsigned s_frameCount = 0;
static bool s_logFrames = false;

// gameFrame ==================================================================
/** One browser frame of the game: the body of GameEngine::execute()'s loop. */
//=============================================================================
static void gameFrame( void * )
{
	try {

		// requestAnimationFrame runs at the display's rate, which can be above the game's fps limit.
		if( !TheFramePacer->isFrameDue() )
			return;

		TheGameEngine->executeFrame();

		++s_frameCount;
		if( s_frameCount % 30 == 0 )
		{
			MAIN_THREAD_ASYNC_EM_ASM( { window.__zhFrames = $0; }, s_frameCount );
			if( s_logFrames && s_frameCount % 300 == 0 )
				printf( "frame %u\n", s_frameCount );
		}

		if( TheGameEngine->getQuitting() )
		{
			emscripten_cancel_main_loop();
			finishGame();
			shutdownApplication( 0 );
		}
	}
	catch (...)
	{
		// An exception that reaches the frame loop is a failure of the game, not of one frame.
		fprintf( stderr, "Fatal error: unhandled exception in game frame %u\n", s_frameCount );
		emscripten_cancel_main_loop();
		WebPlatform_NotifyExit( 1 );
	}
}

// runGame ====================================================================
/** The counterpart of GameMain(): creates and initializes the engine, then runs
	* it. Returns true when the game was only started and runs frame by frame
	* from now on (see gameFrame), and false when it already ended, with the
	* exit code in exitcode. */
//=============================================================================
static Bool runGame( Int &exitcode )
{
	exitcode = 0;

	TheFramePacer = new FramePacer();
	TheFramePacer->enableFramesPerSecondLimit(TRUE);
	TheGameEngine = CreateGameEngine();
	TheGameEngine->init();

	if (!TheGlobalData->m_simulateReplays.empty())
	{
		// Headless: nothing is shown, so the plain blocking loop is fine.
		exitcode = ReplaySimulation::simulateReplays(TheGlobalData->m_simulateReplays, TheGlobalData->m_simulateReplayJobs);
	}
	else if (TheGlobalData->m_headless)
	{
		TheGameEngine->execute();
	}
	else
	{
		// Frames are driven by requestAnimationFrame (fps 0); the stack is not unwound
		// (simulate_infinite_loop 0), main() returns and the thread stays alive for the frames.
		emscripten_set_main_loop_arg( gameFrame, nullptr, 0, 0 );
		return true;
	}

	finishGame();
	return false;
}

// main =======================================================================
/** Application entry point, on the engine thread */
//=============================================================================
int main( int argc, char **argv )
{
	Int exitcode = 1;

	// The game reads its arguments from the C runtime's globals, like on Windows.
	__argc = argc;
	__argv = argv;

	// Fatal errors are shown by the page. Set before anything can fail.
	DebugSetCrashHandler( reportFatal );
	for( int i = 1; i < argc; ++i )
	{
		if( strcmp( argv[i], "-webframelog" ) == 0 )
			s_logFrames = true;
	}

	try {

		TheAsciiStringCriticalSection = &critSec1;
		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		// initialize the memory manager early
		initMemoryManager();

		// browser events, and the game files
		if( !WebPlatform_Init( nullptr ) )
		{
			fprintf( stderr, "Could not register the browser input handlers.\n" );
		}

		if( WebPlatform_MountStorage() != 0 )
		{
			fprintf( stderr, "Could not open the game files. Import them from the start page first.\n" );
			return exitcode;
		}

		// All the game's relative paths ("Data\\INI\\...", "Maps\\...") are relative to the install.
		if( !rts::WorkingDirectory::setCustomWorkingDirectory( GAME_DIRECTORY ) )
		{
			fprintf( stderr, "Could not enter %s.\n", GAME_DIRECTORY );
			return exitcode;
		}

		// The debug log (debug builds only) goes to the console and to a file in the user data;
		// the memory manager could not start it any earlier, see GameMemory.cpp.
		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);

		CommandLine::parseCommandLineForStartup();

		// The canvas is the one and only window, always "windowed".
		TheWritableGlobalData->m_windowed = TRUE;

		WebPlatform_SetWindowProc( WebWndProc );
		WebPlatform_SetTitle( "Command and Conquer Generals Zero Hour" );

		// The renderer (Direct3D 8 on WebGL2) draws on the page's canvas, tells the page when the
		// back buffer size changes, and leaves presenting the frame to the browser: the frames
		// end by returning to the event loop, see gameFrame().
		WebD3D8_PlatformHooks d3dHooks = {};
		d3dHooks.OnClientSize = []( unsigned width, unsigned height ) { WebPlatform_SetClientSize( (int)width, (int)height ); };
		WebD3D8_SetPlatformHooks( &d3dHooks );
		WebD3D8_SetCanvas( WebPlatform_GetCanvasSelector() );
		WebD3D8_SetPresentMode( WEBD3D8_PRESENT_IMPLICIT );

		// The Win32 mouse code and the renderer want a window handle, like in WinMain.
		ApplicationHInstance = (HINSTANCE)0x00400000;
		if( !TheGlobalData->m_headless )
		{
			ApplicationHWnd = (HWND)WebPlatform_GetWindow();
		}
		isWinMainActive = WebPlatform_IsActive() != 0;

		// Set up version info
		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

		DEBUG_LOG(("CRC message is %d", GameMessage::MSG_LOGIC_CRC));

		// run the game main loop
		if( runGame( exitcode ) )
		{
			// The game runs from the browser's frames now; they shut down when it ends.
			return 0;
		}
	}
	catch (...)
	{
		// The Windows version swallows this silently; in the browser nobody would learn why
		// the game never started. Most of these are INI or file errors thrown by the engine's
		// start-up, which log the details (see the lines above) before throwing.
		fprintf( stderr, "Fatal error: the game failed to start (exception during start-up)\n" );
		fflush( stderr );
		exitcode = 1;
	}

	shutdownApplication( exitcode );

	return exitcode;

}

// CreateGameEngine ===========================================================
/** Create the web game engine we're going to use */
//=============================================================================
GameEngine *CreateGameEngine()
{
	WebGameEngine *engine;

	engine = NEW WebGameEngine;
	//game engine may not have existed when the page got focus so make sure it
	//knows about current focus state.
	engine->setIsActive(isWinMainActive);

	return engine;

}
