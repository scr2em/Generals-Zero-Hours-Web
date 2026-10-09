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

// AssistUIProtect.cpp
// The client side of the protect assist: the Protect / Stop buttons, the "pick what to protect" mode (click a unit or
// building, press a control group number, or take the current selection), and the links drawn for selected protectors.
// The orders go out as MSG_ASSIST_PROTECT / MSG_ASSIST_UNPROTECT; the links live in the simulation (GameLogic/Protect.h).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/Protect.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	enum { BTN_PROTECT = 1, BTN_STOP = 2, BTN_SELECTION = 3, BTN_CANCEL = 4 };

	// the "what to protect" mode
	Bool							s_picking = FALSE;
	Bool							s_swallowClick = FALSE;		// the click that follows the button release that ended the mode belongs to the mode too
	std::vector<ObjectID>	s_protectors;			// the units that will protect, fixed when the mode starts
}

//-------------------------------------------------------------------------------------------------
static void sendProtect( Int squad, const std::vector<ObjectID> &protectors, const std::vector<ObjectID> &targets )
{
	if (protectors.empty() || (squad < 0 && targets.empty()))
		return;
	GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_PROTECT );
	m->appendIntegerArgument( squad );
	m->appendIntegerArgument( (Int)protectors.size() );
	for (size_t i = 0; i < protectors.size(); ++i)
		m->appendObjectIDArgument( protectors[i] );
	for (size_t i = 0; i < targets.size(); ++i)
		m->appendObjectIDArgument( targets[i] );
}

//-------------------------------------------------------------------------------------------------
class ProtectPanel : public AssistPanel
{
public:
	ProtectPanel() : AssistPanel( "Protect" )
	{
		m_title = L"Protect";
		setStackLeft( TRUE );
		addButton( BTN_PROTECT, 0, L"Protect...", nullptr );
		addButton( BTN_STOP, 0, L"Stop", nullptr );
		addButton( BTN_SELECTION, 0, L"Selection", L"Protect the units that are selected now" );
		addButton( BTN_CANCEL, 0, L"Cancel", nullptr );
	}

	virtual Bool refresh() override
	{
		if (!TheAssistOptions.m_protect || !AssistUI::active())
		{
			s_picking = FALSE;
			return FALSE;
		}
		const AssistUI::Selection &sel = AssistUI::selection();
		if (s_picking)
		{
			// the protectors are fixed; the selection may be the group to protect now
			find( BTN_PROTECT )->m_visible = FALSE;
			find( BTN_STOP )->m_visible = FALSE;
			find( BTN_SELECTION )->m_visible = TRUE;
			find( BTN_CANCEL )->m_visible = TRUE;
			m_title = L"Protect: pick what to protect";
			return TRUE;
		}
		if (sel.m_ids.empty())
			return FALSE;

		Bool anyLink = FALSE;
		for (size_t i = 0; i < sel.m_ids.size() && !anyLink; ++i)
			anyLink = ThePlayerAssist->protect().isProtector( sel.m_ids[i] );
		find( BTN_PROTECT )->m_visible = TRUE;
		find( BTN_STOP )->m_visible = TRUE;
		find( BTN_STOP )->m_enabled = anyLink;
		find( BTN_SELECTION )->m_visible = FALSE;
		find( BTN_CANCEL )->m_visible = FALSE;
		for (size_t i = 0; i < m_buttons.size(); ++i)
			m_buttons[i].m_on = FALSE;
		m_title = L"Protect";
		return TRUE;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		flow( 4, AssistUI::px( 74 ), AssistUI::px( 22 ), AssistUI::px( 3 ), w, h );
		*x = AssistUI::px( 6 );
		*y = AssistUI::stackY( this, *h );
	}

	virtual void clicked( Int id ) override
	{
		switch (id)
		{
			case BTN_PROTECT:	AssistProtectUI::begin(); break;
			case BTN_STOP:			AssistProtectUI::stop(); break;
			case BTN_SELECTION:	AssistProtectUI::useSelection(); break;
			case BTN_CANCEL:		AssistProtectUI::cancel(); break;
		}
	}
};

static ProtectPanel *s_panel = nullptr;

//-------------------------------------------------------------------------------------------------
void AssistProtectUI::init()
{
	if (s_panel == nullptr)
		s_panel = new ProtectPanel;
}

//-------------------------------------------------------------------------------------------------
void AssistProtectUI::reset()
{
	s_picking = FALSE;
	s_swallowClick = FALSE;
	s_protectors.clear();
}

//-------------------------------------------------------------------------------------------------
Bool AssistProtectUI::picking()
{
	return s_picking;
}

//-------------------------------------------------------------------------------------------------
void AssistProtectUI::begin()
{
	if (!TheAssistOptions.m_protect || !AssistUI::active())
		return;
	const AssistUI::Selection &sel = AssistUI::selection();
	if (sel.m_ids.empty())
		return;
	s_protectors = sel.m_ids;
	s_picking = TRUE;
}

//-------------------------------------------------------------------------------------------------
void AssistProtectUI::cancel()
{
	s_picking = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AssistProtectUI::stop()
{
	const AssistUI::Selection &sel = AssistUI::selection();
	Bool any = FALSE;
	for (size_t i = 0; i < sel.m_ids.size(); ++i)
		any = any || ThePlayerAssist->protect().isProtector( sel.m_ids[i] );
	if (any)
		TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_UNPROTECT );
}

//-------------------------------------------------------------------------------------------------
/// The units that are selected now are the group to protect (the protectors themselves excluded).
void AssistProtectUI::useSelection()
{
	if (!s_picking)
		return;
	const AssistUI::Selection &sel = AssistUI::selection();
	std::vector<ObjectID> targets;
	const DrawableList *list = TheInGameUI->getAllSelectedLocalDrawables();
	if (list)
	{
		for (DrawableListCIt it = list->begin(); it != list->end(); ++it)
		{
			const Object *o = (*it)->getObject();
			if (o && std::find( s_protectors.begin(), s_protectors.end(), o->getID() ) == s_protectors.end())
				targets.push_back( o->getID() );
		}
	}
	(void)sel;
	sendProtect( -1, s_protectors, targets );
	s_picking = FALSE;
}

//-------------------------------------------------------------------------------------------------
/// Hotkeys and clicks while the mode is on.  Returns true when the message is used up.
Bool AssistProtectUI::translate( const GameMessage *msg )
{
	const GameMessage::Type t = msg->getType();

	if ((t == GameMessage::MSG_META_ASSIST_PROTECT || t == GameMessage::MSG_META_ASSIST_UNPROTECT) && (!TheAssistOptions.m_protect || !AssistUI::active()))
		return FALSE;

	if (t == GameMessage::MSG_META_ASSIST_PROTECT)
	{
		if (s_picking)
			cancel();
		else
			begin();
		return TRUE;
	}
	if (t == GameMessage::MSG_META_ASSIST_UNPROTECT)
	{
		stop();
		return TRUE;
	}

	if (s_swallowClick && (t == GameMessage::MSG_MOUSE_LEFT_CLICK || t == GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK))
	{
		s_swallowClick = FALSE;
		return TRUE;
	}

	if (!s_picking)
		return FALSE;

	// a control group number: protect that group (new members join it)
	if (t >= GameMessage::MSG_META_SELECT_TEAM0 && t <= GameMessage::MSG_META_SELECT_TEAM9)
	{
		sendProtect( (Int)(t - GameMessage::MSG_META_SELECT_TEAM0), s_protectors, std::vector<ObjectID>() );
		s_picking = FALSE;
		return TRUE;
	}
	// Enter: the selected units
	if (t == GameMessage::MSG_META_CHAT_EVERYONE)
	{
		useSelection();
		return TRUE;
	}
	// Escape
	if (t == GameMessage::MSG_META_OPTIONS)
	{
		cancel();
		return TRUE;
	}

	// a click on a unit or building (the click goes only to this mode; a click on the ground ends it)
	if (t == GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN || t == GameMessage::MSG_MOUSE_LEFT_CLICK || t == GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK)
		return TRUE;
	if (t == GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP)
	{
		const ICoord2D pixel = msg->getArgument( 0 )->pixel;
		Drawable *d = TheTacticalView ? TheTacticalView->pickDrawable( &pixel, FALSE, PICK_TYPE_SELECTABLE ) : nullptr;
		const Object *o = d ? d->getObject() : nullptr;
		Player *local = ThePlayerList->getLocalPlayer();
		if (o && local && local->getRelationship( o->getTeam() ) != ENEMIES &&
				std::find( s_protectors.begin(), s_protectors.end(), o->getID() ) == s_protectors.end())
		{
			std::vector<ObjectID> targets;
			targets.push_back( o->getID() );
			sendProtect( -1, s_protectors, targets );
		}
		s_picking = FALSE;
		s_swallowClick = TRUE;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static void shield( Int x, Int y, Int size, Color color )
{
	// a small shield: flat top, pointed bottom
	const Int h = size, w = size * 4 / 5;
	AssistUI::line( x - w / 2, y - h / 2, x + w / 2, y - h / 2, 1, color );
	AssistUI::line( x + w / 2, y - h / 2, x + w / 2, y, 1, color );
	AssistUI::line( x + w / 2, y, x, y + h / 2, 1, color );
	AssistUI::line( x, y + h / 2, x - w / 2, y, 1, color );
	AssistUI::line( x - w / 2, y, x - w / 2, y - h / 2, 1, color );
}

//-------------------------------------------------------------------------------------------------
static Bool screenOf( const Coord3D &world, ICoord2D *s )
{
	Coord3D p = world;
	p.z = TheTerrainLogic->getGroundHeight( p.x, p.y ) + 10.0f;
	return TheTacticalView->worldToScreen( &p, s );
}

//-------------------------------------------------------------------------------------------------
/// Links of the selected protectors: a line to each protected object, a shield on both ends, the home.
void AssistProtectUI::drawOverlays( View *view )
{
	if (!TheAssistOptions.m_protect || TheTacticalView == nullptr)
		return;

	const AssistUI::Selection &sel = AssistUI::selection();
	const Color lineColor = GameMakeColor( 120, 190, 255, 200 );
	const Color shieldColor = GameMakeColor( 140, 210, 255, 255 );
	const Int s = AssistUI::px( 11 );

	for (size_t i = 0; i < sel.m_ids.size(); ++i)
	{
		const ProtectLink *link = ThePlayerAssist->protect().find( sel.m_ids[i] );
		const Object *protector = TheGameLogic->findObjectByID( sel.m_ids[i] );
		if (link == nullptr || protector == nullptr)
			continue;

		ICoord2D ps;
		if (!screenOf( *protector->getPosition(), &ps ))
			continue;
		ps.y -= AssistUI::px( 26 );
		shield( ps.x, ps.y, s, link->m_state == PROTECT_RESPONDING ? GameMakeColor( 255, 120, 90, 255 ) : shieldColor );

		std::vector<ObjectID> targets;
		ThePlayerAssist->protect().resolveTargets( *link, targets );
		for (size_t t = 0; t < targets.size(); ++t)
		{
			const Object *o = TheGameLogic->findObjectByID( targets[t] );
			ICoord2D ts;
			if (o == nullptr || !screenOf( *o->getPosition(), &ts ))
				continue;
			AssistUI::line( ps.x, ps.y, ts.x, ts.y - AssistUI::px( 24 ), 1, lineColor );
			shield( ts.x, ts.y - AssistUI::px( 24 ), s * 3 / 4, shieldColor );
		}

		// where the protector stays
		ICoord2D hs;
		if (screenOf( link->m_home, &hs ))
		{
			const Int r = AssistUI::px( 5 );
			AssistUI::line( hs.x - r, hs.y, hs.x, hs.y - r, 1, shieldColor );
			AssistUI::line( hs.x, hs.y - r, hs.x + r, hs.y, 1, shieldColor );
			AssistUI::line( hs.x + r, hs.y, hs.x, hs.y + r, 1, shieldColor );
			AssistUI::line( hs.x, hs.y + r, hs.x - r, hs.y, 1, shieldColor );
		}
	}

	// the hint while picking
	if (s_picking)
	{
		const UnicodeString hint = L"Protect: click a unit or building, press a control group number, Enter = the selected units, Esc = cancel";
		const Int w = AssistUI::textWidth( hint, 11 );
		const Int x = ((Int)TheDisplay->getWidth() - w) / 2;
		const Int y = AssistUI::px( 40 );
		AssistUI::box( x - 8, y - 4, w + 16, AssistUI::px( 20 ), GameMakeColor( 10, 20, 40, 220 ), GameMakeColor( 140, 210, 255, 255 ) );
		AssistUI::text( hint, x, y, GameMakeColor( 255, 255, 255, 255 ), 11 );
	}
}
