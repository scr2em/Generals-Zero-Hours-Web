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
// main() runs on a worker thread (-sPROXY_TO_PTHREAD), on a JSPI stack (-sJSPI) that can
// suspend, and runs the engine's frame loop on it: one frame of the game, then wait for the
// browser's next animation frame. The browser only shows a frame once the thread returns to its
// event loop, which a suspended JSPI stack does. The same goes for the game's own blocking loops
// that render from inside themselves (load screen, fades, movies): the Direct3D 8 layer reports
// every Present() to WebPlatform_FramePresented(), which suspends the thread until the next
// display frame, so those loops show their frames without being restructured (see WebPlatform.h).
// There is no window to create: the page provides a canvas, WebPlatform turns its input into the
// same window messages Windows would deliver, and WebWndProc below is the game's window procedure.
//
// Data: the page either copied the player's game files into the Origin Private File
// System before starting us, or (-webdirect) handed us the files of the folder the
// player picked, to be read in place; WebPlatform_MountStorage() makes them appear as
// /game (Zero Hour), /generals (Generals) and /userdata (saves, options).
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include <emscripten/heap.h>

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
#include "GameClient/IMEManager.h"
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
		// First let the IME manager do its stuff: it turns WM_CHAR into the characters of the text entry fields.
		if ( TheIMEManager )
		{
			if ( TheIMEManager->serviceIMEMessage( (HWND)hWnd, message, (Int)wParam, (Int)lParam ) )
				return TheIMEManager->result();
		}

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
	// ReleaseCrashLocalized() has no text to look up this early and passes the string's key on.
	if( message && strcmp( message, "ERROR:D3DFailureMessage" ) == 0 )
		message = "The renderer could not start. This browser or graphics driver does not provide the WebGL 2 features the game needs.";

	// Debug builds route failed asserts here too; they carry on, release crashes end the game.
	const bool assertion = message && strncmp( message, "ASSERTION FAILURE", 17 ) == 0;
	fprintf( stderr, "%s: %s\n", assertion ? "Assertion failed" : "Fatal error", message ? message : "(no message)" );
	fflush( stderr );
}

// Frames run since the game started, for the page (window.__zhFrames, about twice a second)
// and for the log when -webframelog is on the command line.
static unsigned s_frameCount = 0;
static double s_busyMs = 0.0;	// time spent in executeFrame() since the last report
static bool s_logFrames = false;
static bool s_logDirectStats = false;	// -webdirectstats: the read counters of the direct file mode, see WebStorage.cpp

// runFrames ==================================================================
/** The body of GameEngine::execute()'s loop, paced by the browser: one frame of the game per
	* display frame. A frame ends by waiting for the next display frame, which is what shows it
	* (the frame's Present() already does that, see WebPlatform_FramePresented). Blocking loops
	* inside a frame that render show their frames the same way.
	* Returns true when the game ended normally, false when it failed. */
//=============================================================================
static bool runFrames()
{
	try {

		for(;;)
		{
			static unsigned s_calls = 0;
			if( ++s_calls == 1 || s_calls == 100 )
				DEBUG_LOG(("gameFrame call %u", s_calls));

			// Display frames come at the display's rate, which can be above the game's fps limit.
			if( !TheFramePacer->isFrameDue() )
			{
				WebPlatform_WaitFrame();
				continue;
			}

			const double frameStart = emscripten_get_now();
			const double yieldedBefore = WebPlatform_GetYieldedMs();
			WebPlatform_BeginFrame();
			TheGameEngine->executeFrame();
			WebPlatform_EndFrame();
			// The time the thread was suspended is the browser's, not the game's.
			s_busyMs += emscripten_get_now() - frameStart - ( WebPlatform_GetYieldedMs() - yieldedBefore );

			++s_frameCount;
			if( s_frameCount == 1 )
			{
				DEBUG_LOG(("First frame done"));
				MAIN_THREAD_ASYNC_EM_ASM( { window.__zhFirstFrameAt = Date.now(); } );
			}
			if( s_logDirectStats && ( s_frameCount == 1 || s_frameCount % 300 == 0 ) )
			{
				char stats[512];
				if( WebPlatform_GetDirectStats( stats, sizeof( stats ) ) > 0 )
					printf( "direct file stats at frame %u: %s\n", s_frameCount, stats );
			}
			if( s_frameCount % 30 == 0 )
			{
				MAIN_THREAD_ASYNC_EM_ASM( { window.__zhFrames = $0; window.__zhHeapBytes = $1; window.__zhFrameMs = $2; window.__zhYields = $3; },
					s_frameCount, (unsigned)emscripten_get_heap_size(), s_busyMs / 30.0, WebPlatform_GetYieldCount() );
				s_busyMs = 0.0;
				if( s_logFrames && s_frameCount % 300 == 0 )
					printf( "frame %u (%u waits for the browser)\n", s_frameCount, WebPlatform_GetYieldCount() );
			}

			if( TheGameEngine->getQuitting() )
				return true;
		}
	}
	catch (...)
	{
		// An exception that reaches the frame loop is a failure of the game, not of one frame.
		fprintf( stderr, "Fatal error: unhandled exception in game frame %u\n", s_frameCount );
		return false;
	}
}

// runGame ====================================================================
/** The counterpart of GameMain(): creates and initializes the engine, then runs
	* it. Returns true when the game ended and was shut down here (the page was told),
	* and false when it only ended and main() has to shut down, with the exit code in
	* exitcode. */
//=============================================================================
static Bool runGame( Int &exitcode )
{
	exitcode = 0;

	TheFramePacer = new FramePacer();
	TheFramePacer->enableFramesPerSecondLimit(TRUE);
	TheGameEngine = CreateGameEngine();
	TheGameEngine->init();
	DEBUG_LOG(("Game engine initialized, starting the frame loop (fps limit %d, actual %d, enabled %d)",
		TheFramePacer->getFramesPerSecondLimit(), TheFramePacer->getActualFramesPerSecondLimit(), (int)TheFramePacer->isActualFramesPerSecondLimitEnabled()));

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
		// This thread suspends (JSPI) to let the browser show the frames, see runFrames().
		if( runFrames() )
		{
			finishGame();
			shutdownApplication( 0 );
		}
		else
		{
			// The engine is in an unknown state: tell the page and leave everything as it is.
			WebPlatform_NotifyExit( 1 );
		}
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
		if( strcmp( argv[i], "-webinputlog" ) == 0 )
			WebPlatform_SetInputLog( 1 );
		if( strcmp( argv[i], "-webdirect" ) == 0 )
			WebPlatform_RequireDirectStorage( 1 );
		if( strcmp( argv[i], "-webyieldlog" ) == 0 )
			WebPlatform_SetYieldLog( 1 );
		if( strcmp( argv[i], "-webdirectstats" ) == 0 )
			s_logDirectStats = true;
		if( strncmp( argv[i], "-webd3d8debug", 13 ) == 0 )
			WebD3D8_SetDebug( argv[i][13] == '=' ? atoi( argv[i] + 14 ) | 1 : 1 );	// see WebD3D8.h
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
			fprintf( stderr, "Could not open the game files. Import them or open your game folder from the start page first.\n" );
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
		// back buffer size changes, and leaves presenting the frame to the browser: the browser
		// shows it when this thread suspends to its event loop, see runFrames().
		WebD3D8_PlatformHooks d3dHooks = {};
		d3dHooks.OnClientSize = []( unsigned width, unsigned height ) { WebPlatform_SetClientSize( (int)width, (int)height ); };
		// Every Present() lets the browser show the frame: the frame loop's pacing and, from inside
		// blocking loops that render (load screens, movies), their progress.
		d3dHooks.OnFramePresented = []() { WebPlatform_FramePresented(); };
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

		// From here on this thread suspends (JSPI) to let the browser show the frames.
		WebPlatform_SetYieldEnabled( 1 );

		// run the game main loop
		if( runGame( exitcode ) )
		{
			// The game ran from the browser's frames and shut down when it ended.
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
