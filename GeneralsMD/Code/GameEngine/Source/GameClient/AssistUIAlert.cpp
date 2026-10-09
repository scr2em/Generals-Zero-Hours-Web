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

// AssistUIAlert.cpp
// The "base under attack" response: when the game's own under attack alert fires, a button offers to send the idle army
// units near the attacked place to attack-move there; a second click sends them back to where they were.  The alert
// is the one the radar raises (Radar::tryUnderAttackEvent); the orders are the commands MSG_ASSIST_BASE_DEFEND and
// MSG_ASSIST_BASE_RETURN, carried out by the simulation (PlayerAssist::baseDefend).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/AssistHooks.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	enum { BTN_DEFEND = 1, BTN_RETURN = 2 };

	Bool				s_alert = FALSE;
	Coord3D			s_alertPos;
	UnsignedInt	s_alertFrame = 0;
	const UnsignedInt ALERT_FRAMES = 40 * LOGICFRAMES_PER_SECOND;	// the button stays this long after the last alert

	/// Is a response out (units were sent and not yet called back)?
	Bool responseOut()
	{
		Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		return local && ThePlayerAssist && ThePlayerAssist->defendActive( local->getPlayerIndex() );
	}

	Bool shown()
	{
		if (!TheAssistOptions.m_baseAlert || !AssistUI::active())
			return FALSE;
		if (responseOut())
			return TRUE;
		return s_alert && TheGameLogic->getFrame() - s_alertFrame < ALERT_FRAMES;
	}
}

//-------------------------------------------------------------------------------------------------
/// The radar raised the under attack alert for the local player (a hook called from Core's Radar).
static void alertHook( const Coord3D *pos )
{
	if (pos == nullptr)
		return;
	s_alert = TRUE;
	s_alertPos = *pos;
	s_alertFrame = TheGameLogic->getFrame();
	ASSIST_DEBUG(( "ASSIST alert raised at %.0f,%.0f", pos->x, pos->y ));
}

//-------------------------------------------------------------------------------------------------
class AlertPanel : public AssistPanel
{
public:
	AlertPanel() : AssistPanel( "BaseAlert" )
	{
		m_title = L"Base under attack!";
		addButton( BTN_DEFEND, 0, L"Defend", L"Send idle army units near the attack to attack-move there" );
		addButton( BTN_RETURN, 0, L"Send back", L"Send them back to where they were" );
	}

	virtual Bool refresh() override
	{
		if (!shown())
			return FALSE;
		const Bool out = responseOut();
		find( BTN_DEFEND )->m_visible = !out;
		find( BTN_RETURN )->m_visible = out;
		m_title = out ? L"Defenders sent" : L"Base under attack!";
		return TRUE;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		flow( 1, AssistUI::px( 150 ), AssistUI::px( 26 ), AssistUI::px( 3 ), w, h );
		*x = ((Int)TheDisplay->getWidth() - *w) / 2;
		*y = AssistUI::px( 26 );
	}

	virtual void clicked( Int id ) override
	{
		if (id == BTN_DEFEND)
			AssistAlertUI::defend();
		else
			AssistAlertUI::sendBack();
	}

	virtual void drawGlyph( const AssistButton &b, Int x, Int y, Color color ) override
	{
		const Color c = b.m_id == BTN_DEFEND ? GameMakeColor( 255, 190, 120, 255 ) : GameMakeColor( 170, 220, 255, 255 );
		AssistUI::text( b.m_label, x + b.m_x, y + b.m_y + (b.m_h - AssistUI::px( 13 )) / 2, c, 11, TRUE, b.m_w );
	}
};

static AlertPanel *s_panel = nullptr;

//-------------------------------------------------------------------------------------------------
void AssistAlertUI::init()
{
	TheAssistAlertHook = &alertHook;
	if (s_panel == nullptr)
		s_panel = new AlertPanel;
}

//-------------------------------------------------------------------------------------------------
void AssistAlertUI::reset()
{
	s_alert = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AssistAlertUI::defend()
{
	if (!TheAssistOptions.m_baseAlert || !AssistUI::active() || !s_alert)
		return;
	GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_BASE_DEFEND );
	m->appendLocationArgument( s_alertPos );
}

//-------------------------------------------------------------------------------------------------
void AssistAlertUI::sendBack()
{
	if (!TheAssistOptions.m_baseAlert || !AssistUI::active())
		return;
	TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_BASE_RETURN );
}

//-------------------------------------------------------------------------------------------------
Bool AssistAlertUI::translate( const GameMessage *msg )
{
	if (msg->getType() != GameMessage::MSG_META_ASSIST_BASE_DEFEND)
		return FALSE;
	if (!TheAssistOptions.m_baseAlert || !AssistUI::active())
		return FALSE;
	// the hotkey is the button: defend, or send back when a response is out
	if (responseOut())
		sendBack();
	else
		defend();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AssistAlertUI::drawOverlays( View *view )
{
	if (!shown() || TheTacticalView == nullptr)
		return;

	// a mark where the attack is
	Coord3D p = s_alertPos;
	p.z = TheTerrainLogic->getGroundHeight( p.x, p.y ) + 20.0f;
	ICoord2D s;
	if (!TheTacticalView->worldToScreen( &p, &s ))
		return;
	const UnsignedInt phase = (TheGameLogic->getFrame() / 6) % 4;
	const Int r = AssistUI::px( 14 + (Int)phase * 3 );
	const Color red = GameMakeColor( 255, 70, 50, 255 );
	AssistUI::line( s.x - r, s.y - r, s.x + r, s.y - r, 2, red );
	AssistUI::line( s.x + r, s.y - r, s.x + r, s.y + r, 2, red );
	AssistUI::line( s.x + r, s.y + r, s.x - r, s.y + r, 2, red );
	AssistUI::line( s.x - r, s.y + r, s.x - r, s.y - r, 2, red );
}
