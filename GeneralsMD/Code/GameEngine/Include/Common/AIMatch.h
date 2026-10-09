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

// FILE: AIMatch.h ////////////////////////////////////////////////////////////
//
// The AI-vs-AI test bench: a command line driven match mode that starts a skirmish between
// computer players only, runs the game logic as fast as the machine allows without rendering,
// ends on victory or after a frame limit, and reports statistics as JSON.
//
//   <game> -aiMatch map=<map> players=<difficulty>:<side>[:<team>[:<start>[:<variant>]]],... seed=<n>
//          [timeout=<minutes of game time> | maxframes=<logic frames>] [stats=<file.json>]
//          [crcinterval=<frames>] [sample=<frames>] [idleinterval=<frames>] [progress=<frames>]
//          [cash=<starting money>] [label=<text>] [record=1] [loop=logic|engine] [eliminate=<slot>@<frame>]
//
// The mode implies -headless. The result is printed on one line, "AIMATCH_RESULT {json}", and
// also written to stats=<file> when given. The command line, the statistics and the way a match is
// driven are described in scripts/aibench/README.md.
//
// A match is deterministic: it uses nothing but the command line (seed → game logic random seed)
// and the game data, so the same command line plays the same match, frame for frame, with the
// same build. The CRC timeline in the statistics proves it.
//
// This module only uses the engine's own interfaces and C stdio, so it builds on every platform
// the engine does; the entry points are called from GameMain() (Windows) and WebMain.cpp (web).
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

class Player;

class AIMatch
{
public:

	/// True if -aiMatch is on the command line.
	static Bool isRequested();

	/// True while a match is being played (from run() until it returns). Engine code that must behave
	/// differently without a user interface (load screen, replay recording) asks this.
	static Bool isActive();

	/// Plays the match described by the command line and writes its statistics. Needs the engine to be
	/// initialized (TheGameEngine->init()). Returns the exit code of the process: 0 if the match was played
	/// to its end (victory or frame limit), 1 if it could not be played.
	static Int run();

	/// True if the match should be recorded as a replay (LastReplay.rep) like a normal skirmish.
	/// Matches are not recorded unless record=1 is given: parallel matches would fight over the file.
	static Bool shouldRecordReplay();

	/// The free-form tag given to a computer player on the command line (the fifth field of its entry in
	/// players=). AI code that wants to run an experimental variant next to the standard one in the same match
	/// reads it; it is empty for every player outside of a test bench match.
	static AsciiString getPlayerVariant(const Player *player);
};
