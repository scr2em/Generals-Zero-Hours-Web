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

// FILE: NativeMain.cpp ///////////////////////////////////////////////////////
//
// Entry point of the native headless build (cmake/native-headless.cmake), the counterpart of
// WebMain.cpp for a command line program on Linux and macOS. It always runs headless: no window,
// renderer, audio or video. It is meant for the automated test modes (-aiMatch, see
// scripts/aibench/README.md; -assistMatch, see scripts/assistbench/README.md; -simulateReplay), which run at
// full speed and print their results.
//
//   zh_headless [--zh DIR] [--generals DIR] [--userdata DIR] <engine arguments>
//
//   --zh DIR        the Zero Hour install (the folder with INIZH.big, or the starter content);
//                   default $ZH_PATH
//   --generals DIR  the original Generals install, whose archives Zero Hour also reads (optional);
//                   default $GENERALS_PATH
//   --userdata DIR  where the game keeps its user data (options, replays); default a new temporary
//                   folder that is removed at the end
//
// The game files are read in place, like the web build's "read in place" mode, with the case
// insensitive Windows file names the game uses resolved against the real names on disk.
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <windows.h>
#include <webcompat_folders.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "WinMain.h"
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/FramePacer.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "Common/Debug.h"
#include "Common/Errors.h"
#include "Common/GameMemory.h"
#include "Common/MessageStream.h"
#include "Common/ReplaySimulation.h"
#include "Common/AIMatch.h"
#include "Common/AssistMatch.h"
#include "Common/WorkingDirectory.h"
#include "GameLogic/GameLogic.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "WebDevice/Common/WebGameEngine.h"
#include "WebDevice/Platform/WebPlatform.h"
#include "Common/version.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"

// The command line the game parses, see CommandLine.cpp. WebCompat defines them.
extern int __argc;
extern char **__argv;

// GLOBALS ////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;  ///< our application instance
HWND ApplicationHWnd = nullptr;  ///< there is no window
Win32Mouse *TheWin32Mouse = nullptr;  ///< for the WndProc() only
DWORD TheMessageTime = 0;	///< the time a message was posted

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
const char *gAppPrefix = ""; /// So WB can have a different debug log file name.

// Necessary to allow memory managers and such to have useful critical sections
static CriticalSection critSec1, critSec2, critSec3, critSec4, critSec5;

namespace
{

// The temporary user data folder, removed at the end; empty when the user chose the folder.
std::filesystem::path s_temporaryUserData;

// NativeWndProc ===============================================================
/** There is no window, so the only messages are the ones the game posts to itself. */
//=============================================================================
intptr_t NativeWndProc( uintptr_t, uint32_t message, uintptr_t, intptr_t )
{
	if( message == WEBWM_CLOSE && TheGameEngine && !TheGameEngine->getQuitting() )
	{
		if( TheMessageStream && TheMessageStream->isReadyForMessages() )
			TheMessageStream->appendMessage( GameMessage::MSG_META_DEMO_INSTANT_QUIT );
		else
			TheGameEngine->setQuitting( TRUE );
	}
	return 0;
}

// reportFatal ================================================================
/** Debug and release crashes (RELEASE_CRASH, failed asserts) end up here instead of in the Windows
	* message box. The game exits right after a release crash (ReleaseCrash ends with _exit). */
//=============================================================================
void reportFatal( const char *message )
{
	const bool assertion = message && strncmp( message, "ASSERTION FAILURE", 17 ) == 0;
	fprintf( stderr, "%s: %s\n", assertion ? "Assertion failed" : "Fatal error", message ? message : "(no message)" );
	fflush( stderr );
}

// describeException ==========================================================
/** Says what the exception that is being handled was. Call it from inside a catch block. */
//=============================================================================
void describeException( const char *what )
{
	try
	{
		throw;
	}
	catch( ErrorCode code )
	{
		switch( code )
		{
			case ERROR_OUT_OF_MEMORY:
				fprintf( stderr, "Fatal error: %s: out of memory\n", what );
				break;
			case ERROR_BAD_INI:
				fprintf( stderr, "Fatal error: %s: the game data has a file the game cannot read (see the lines above)\n", what );
				break;
			default:
				fprintf( stderr, "Fatal error: %s (game error 0x%x)\n", what, (unsigned)code );
				break;
		}
	}
	catch( const std::bad_alloc & )
	{
		fprintf( stderr, "Fatal error: %s: out of memory\n", what );
	}
	catch( const std::exception &e )
	{
		fprintf( stderr, "Fatal error: %s (%s)\n", what, e.what() );
	}
	catch( ... )
	{
		fprintf( stderr, "Fatal error: %s\n", what );
	}
	fflush( stderr );
}

void printUsage()
{
	fprintf( stderr,
		"usage: zh_headless [--zh DIR] [--generals DIR] [--userdata DIR] <engine arguments>\n"
		"  --zh DIR        the Zero Hour install or the starter content (default $ZH_PATH)\n"
		"  --generals DIR  the original Generals install (optional, default $GENERALS_PATH)\n"
		"  --userdata DIR  the user data folder (default: a temporary folder, removed at the end)\n"
		"Example: zh_headless --zh starterpack -aiMatch map=ironwood players=hard:Ironwood,hard:Ironwood seed=1\n"
		"See scripts/aibench/README.md for -aiMatch, scripts/assistbench/README.md for -assistMatch.\n" );
}

// An existing directory as an absolute path, or empty.
std::string absoluteDirectory( const char *path )
{
	if( path == nullptr || *path == '\0' )
		return std::string();
	std::error_code ec;
	std::filesystem::path p = std::filesystem::absolute( path, ec );
	if( ec || !std::filesystem::is_directory( p, ec ) )
		return std::string();
	return std::filesystem::weakly_canonical( p, ec ).string();
}

// Engine arguments that name a file of the host (stats=..., aiini=...) are made absolute: the game runs in the
// Zero Hour folder, not in the folder the program was started from.
std::string absoluteOutputArgument( const char *arg, const std::filesystem::path &startDirectory )
{
	static const char *const s_fileOptions[] = { "stats=", "aiini=" };
	for( const char *option : s_fileOptions )
	{
		const size_t length = strlen( option );
		if( strncmp( arg, option, length ) == 0 && arg[length] != '\0' && arg[length] != '/' )
			return std::string( option ) + ( startDirectory / ( arg + length ) ).string();
	}
	return std::string( arg );
}

void removeTemporaryUserData()
{
	if( s_temporaryUserData.empty() )
		return;
	std::error_code ec;
	std::filesystem::remove_all( s_temporaryUserData, ec );
	s_temporaryUserData.clear();
}

// shutdownApplication ========================================================
/** Everything that follows the end of the game: the counterpart of the code after GameMain() in WinMain. */
//=============================================================================
void shutdownApplication()
{
	try
	{
		delete TheVersion;
		TheVersion = nullptr;
		shutdownMemoryManager();
	}
	catch (...)
	{
	}

	WebPlatform_Shutdown();

	TheUnicodeStringCriticalSection = nullptr;
	TheDmaCriticalSection = nullptr;
	TheMemoryPoolCriticalSection = nullptr;
}

// runGame ====================================================================
/** The counterpart of GameMain(): creates and initializes the engine, then runs the requested mode. */
//=============================================================================
Int runGame()
{
	Int exitcode = 0;

	TheFramePacer = new FramePacer();
	TheFramePacer->enableFramesPerSecondLimit(TRUE);
	TheGameEngine = CreateGameEngine();
	TheGameEngine->init();

	if (AIMatch::isRequested())
	{
		exitcode = AIMatch::run();
	}
	else if (AssistMatch::isRequested())
	{
		exitcode = AssistMatch::run();
	}
	else if (!TheGlobalData->m_simulateReplays.empty())
	{
		exitcode = ReplaySimulation::simulateReplays(TheGlobalData->m_simulateReplays, TheGlobalData->m_simulateReplayJobs);
	}
	else
	{
		TheGameEngine->execute();
	}

	delete TheFramePacer;
	TheFramePacer = nullptr;
	delete TheGameEngine;
	TheGameEngine = nullptr;
	return exitcode;
}

} // namespace

// main =======================================================================
/** Application entry point */
//=============================================================================
int main( int argc, char **argv )
{
	setvbuf( stdout, nullptr, _IOLBF, 0 );

	std::error_code ec;
	const std::filesystem::path startDirectory = std::filesystem::current_path( ec );

	// Our own options; everything else goes to the engine.
	const char *zeroHour = getenv( "ZH_PATH" );
	const char *generals = getenv( "GENERALS_PATH" );
	const char *userData = nullptr;
	std::vector<std::string> engineArgs;
	engineArgs.push_back( argc > 0 ? argv[0] : "zh_headless" );
	// Nothing can be shown: always headless (no intro, no window, no renderer device).
	engineArgs.push_back( "-headless" );
	for( int i = 1; i < argc; ++i )
	{
		const char *arg = argv[i];
		if( strcmp( arg, "--help" ) == 0 || strcmp( arg, "-h" ) == 0 )
		{
			printUsage();
			return 0;
		}
		if( ( strcmp( arg, "--zh" ) == 0 || strcmp( arg, "--generals" ) == 0 || strcmp( arg, "--userdata" ) == 0 ) )
		{
			if( i + 1 >= argc )
			{
				fprintf( stderr, "%s needs a directory\n", arg );
				return 2;
			}
			const char *value = argv[++i];
			if( strcmp( arg, "--zh" ) == 0 ) zeroHour = value;
			else if( strcmp( arg, "--generals" ) == 0 ) generals = value;
			else userData = value;
			continue;
		}
		engineArgs.push_back( absoluteOutputArgument( arg, startDirectory ) );
	}

	const std::string zeroHourDir = absoluteDirectory( zeroHour );
	if( zeroHourDir.empty() )
	{
		fprintf( stderr, "No Zero Hour folder: give it with --zh DIR or in ZH_PATH%s%s.\n",
			zeroHour && *zeroHour ? " (not a folder: " : "", zeroHour && *zeroHour ? zeroHour : "" );
		if( zeroHour && *zeroHour )
			fprintf( stderr, ")\n" );
		printUsage();
		return 2;
	}

	std::string userDataDir;
	if( userData && *userData )
	{
		std::filesystem::create_directories( userData, ec );
		userDataDir = absoluteDirectory( userData );
		if( userDataDir.empty() )
		{
			fprintf( stderr, "Cannot use %s as the user data folder.\n", userData );
			return 2;
		}
	}
	else
	{
		std::string pattern = ( std::filesystem::temp_directory_path( ec ) / "zh_headless.XXXXXX" ).string();
		std::vector<char> buffer( pattern.begin(), pattern.end() );
		buffer.push_back( '\0' );
		if( mkdtemp( buffer.data() ) == nullptr )
		{
			fprintf( stderr, "Cannot create a temporary user data folder (%s).\n", strerror( errno ) );
			return 2;
		}
		s_temporaryUserData = buffer.data();
		userDataDir = s_temporaryUserData.string();
	}

	// Without the original Generals the game finds an empty folder, as on the web without one.
	std::string generalsDir = absoluteDirectory( generals );
	if( generals && *generals && generalsDir.empty() )
	{
		fprintf( stderr, "Not a folder: %s (the Generals install given by --generals or GENERALS_PATH).\n", generals );
		removeTemporaryUserData();
		return 2;
	}
	if( generalsDir.empty() )
	{
		const std::filesystem::path none = std::filesystem::path( userDataDir ) / "NoGeneralsInstall";
		std::filesystem::create_directories( none, ec );
		generalsDir = none.string();
	}

	WebCompat_SetGameFolders( zeroHourDir.c_str(), generalsDir.c_str(), userDataDir.c_str() );

	// The game reads its arguments from the C runtime's globals, like on Windows.
	std::vector<char *> engineArgv;
	for( std::string &arg : engineArgs )
		engineArgv.push_back( arg.data() );
	engineArgv.push_back( nullptr );
	__argc = (int)engineArgs.size();
	__argv = engineArgv.data();

	DebugSetCrashHandler( reportFatal );

	Int exitcode = 1;
	try {

		TheAsciiStringCriticalSection = &critSec1;
		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		initMemoryManager();

		WebPlatform_Init( nullptr );
		WebPlatform_MountStorage();

		// All the game's relative paths ("Data\\INI\\...", "Maps\\...") are relative to the install.
		if( !rts::WorkingDirectory::setCustomWorkingDirectory( zeroHourDir.c_str() ) )
		{
			fprintf( stderr, "Could not enter %s.\n", zeroHourDir.c_str() );
			removeTemporaryUserData();
			return 1;
		}

		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);

		CommandLine::parseCommandLineForStartup();

		TheWritableGlobalData->m_windowed = TRUE;

		WebPlatform_SetWindowProc( NativeWndProc );
		ApplicationHInstance = (HINSTANCE)0x00400000;

		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

		exitcode = runGame();
	}
	catch (...)
	{
		describeException( "the game failed" );
		exitcode = 1;
	}

	shutdownApplication();
	removeTemporaryUserData();
	fflush( stdout );
	fflush( stderr );

	// Leave without running the static destructors, like the web build (EXIT_RUNTIME=0): some of them free memory
	// that shutdownMemoryManager() has already taken back.
	_exit( exitcode );
}

// CreateGameEngine ===========================================================
/** The engine of the web build, with the native platform layer under it */
//=============================================================================
GameEngine *CreateGameEngine()
{
	WebGameEngine *engine = NEW WebGameEngine;
	engine->setIsActive(TRUE);
	return engine;
}
