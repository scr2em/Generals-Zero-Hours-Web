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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/OptionPreferences.h"

AssistOptions TheAssistOptions;

//-------------------------------------------------------------------------------------------------
AssistOptions::AssistOptions()
{
	m_formations = FALSE;
	m_protect = FALSE;
	m_coverage = FALSE;
	m_baseAlert = FALSE;
	m_odds = FALSE;
	m_stances = FALSE;
	m_idleKeys = FALSE;
	m_repeatProduction = FALSE;
	m_repeatReserve = 0;
	m_infoStrip = FALSE;
	m_maxZoomPercent = 100;
	m_uiScalePercent = 100;
	m_debug = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AssistOptions::load()
{
	OptionPreferences prefs;
	m_formations = prefs.getBool( "AssistFormations", FALSE );
	m_protect = prefs.getBool( "AssistProtect", FALSE );
	m_coverage = prefs.getBool( "AssistCoverageView", FALSE );
	m_baseAlert = prefs.getBool( "AssistBaseAlert", FALSE );
	m_odds = prefs.getBool( "AssistOddsMeter", FALSE );
	m_stances = prefs.getBool( "AssistStances", FALSE );
	m_idleKeys = prefs.getBool( "AssistIdleKeys", FALSE );
	m_repeatProduction = prefs.getBool( "AssistRepeatProduction", FALSE );
	m_repeatReserve = prefs.getInt( "AssistRepeatReserve", 0 );
	if (m_repeatReserve < 0)
		m_repeatReserve = 0;
	if (m_repeatReserve > 100000)
		m_repeatReserve = 100000;
	m_infoStrip = prefs.getBool( "AssistInfoStrip", FALSE );
	m_maxZoomPercent = prefs.getInt( "AssistMaxZoomPercent", 100 );
	m_uiScalePercent = prefs.getInt( "AssistUIScalePercent", 100 );
	if (m_maxZoomPercent < 100)
		m_maxZoomPercent = 100;
	if (m_maxZoomPercent > 300)
		m_maxZoomPercent = 300;
	if (m_uiScalePercent < 50)
		m_uiScalePercent = 50;
	if (m_uiScalePercent > 300)
		m_uiScalePercent = 300;
}

//-------------------------------------------------------------------------------------------------
OptionPreferences *GetOptionsMenuPreferences();

void AssistOptions::save() const
{
	// the options screen holds its own copy of Options.ini and writes all of it when the player accepts: keep it in step
	OptionPreferences *open = GetOptionsMenuPreferences();
	if (open)
		copyTo( *open );

	OptionPreferences prefs;
	copyTo( prefs );
	prefs.write();
}

//-------------------------------------------------------------------------------------------------
void AssistOptions::copyTo( UserPreferences &prefs ) const
{
	prefs.setBool( "AssistFormations", m_formations );
	prefs.setBool( "AssistProtect", m_protect );
	prefs.setBool( "AssistCoverageView", m_coverage );
	prefs.setBool( "AssistBaseAlert", m_baseAlert );
	prefs.setBool( "AssistOddsMeter", m_odds );
	prefs.setBool( "AssistStances", m_stances );
	prefs.setBool( "AssistIdleKeys", m_idleKeys );
	prefs.setBool( "AssistRepeatProduction", m_repeatProduction );
	prefs.setInt( "AssistRepeatReserve", m_repeatReserve );
	prefs.setBool( "AssistInfoStrip", m_infoStrip );
	prefs.setInt( "AssistMaxZoomPercent", m_maxZoomPercent );
	prefs.setInt( "AssistUIScalePercent", m_uiScalePercent );
}

//-------------------------------------------------------------------------------------------------
Bool AssistOptions::anyOrderAssist() const
{
	return m_formations || m_protect || m_baseAlert || m_stances || m_repeatProduction;
}
