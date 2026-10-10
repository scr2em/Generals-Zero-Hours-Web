/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// AssistOptions.h
// The player's own switches for the player assists (see GameLogic/PlayerAssist.h), kept in Options.ini.
// All of them are off by default.  They are client side settings: what an assist does to the units is only
// done when the match allows it as well (GameInfo::getPlayerAssistsAllowed), so every player of a match
// plays with the same rules.

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

struct AssistOptions
{
	Bool	m_formations;				///< formation picker, hotkeys and drag to aim
	Bool	m_protect;					///< protect links
	Bool	m_coverage;					///< defence coverage view
	Bool	m_baseAlert;				///< "base under attack" button
	Bool	m_odds;							///< odds meter
	Bool	m_stances;					///< unit stances: kite, retreat, spread, split fire
	Bool	m_idleKeys;					///< idle army / worker hotkeys
	Bool	m_repeatProduction;	///< repeat production toggle on factories
	Int		m_repeatReserve;		///< repeat production never takes the money below this (sent with the repeat commands)
	Bool	m_infoStrip;				///< income, army value, time, actions per minute
	Int		m_maxZoomPercent;		///< how far out the camera may zoom, in percent of the game's setting (100: as usual)
	Int		m_uiScalePercent;		///< scale of the assist panels and the info strip (100: automatic from the resolution)

	// for the automated tests, from the command line (not saved)
	AsciiString	m_testSpec;			///< -assistTest: units to create at the start of a match
	Bool				m_debug;				///< -assistDebug: print what the assists decide

	AssistOptions();
	void load();								///< read Options.ini
	void save() const;					///< write Options.ini
	void copyTo( class UserPreferences &prefs ) const;	///< put the values into a preferences map
	Bool anyOrderAssist() const;	///< an assist that gives orders is switched on (the default of "player assists allowed" for a new skirmish)
};

extern AssistOptions TheAssistOptions;

/// Print a line for the automated tests when -assistDebug is given: ASSIST_DEBUG(( "format", args ))
#define ASSIST_DEBUG(args) do { if (TheAssistOptions.m_debug) { printf args; printf( "\n" ); } } while (0)
