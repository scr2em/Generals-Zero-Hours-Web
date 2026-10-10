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

// FILE: AssistMatch.h ////////////////////////////////////////////////////////
//
// The player assist test bench: a command line driven skirmish with one human player whose player assists are
// allowed, played without rendering at full logic speed, that follows a script of timed steps (create units,
// select them, give assist and ordinary orders the way the user interface sends them, let enemies attack, wait)
// and checks the game state on the way. It prints one line, "ASSISTMATCH_RESULT {json}", with every check.
//
//   <game> -assistMatch map=<map> players=human:<side>[:<team>[:<start>]],<idle|easy|normal|hard|expert>:<side>[...],...
//          seed=<n> steps=<script> [maxframes=<logic frames>] [label=<text>] [assists=1|0] [cash=<money>] [debug=1|0]
//          [retaliation=1|0]
//
// The script language and the checks are described in AssistMatch.cpp and scripts/assistbench/README.md.
// The mode implies -headless and reuses the plumbing of the AI test bench (AIMatchShared.h).
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

class AssistMatch
{
public:
	/// True if -assistMatch is on the command line.
	static Bool isRequested();

	/// Plays the scripted match described by the command line and prints its result. Needs the engine to be
	/// initialized. Returns the exit code of the process: 0 if every check passed, 1 otherwise.
	static Int run();
};
