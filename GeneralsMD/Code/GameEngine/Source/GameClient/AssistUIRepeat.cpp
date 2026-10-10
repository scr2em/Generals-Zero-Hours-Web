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

// AssistUIRepeat.cpp
// The client side of repeat production: a panel above the command bar for the selected production buildings with a
// "Repeat" switch (and the hotkey), and a tag over each of the player's buildings that repeats.  The switch sends
// MSG_ASSIST_REPEAT_PRODUCTION with the money reserve of the options; what repeats is in the simulation
// (GameLogic/AssistRepeat.cpp).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"

namespace
{
	enum { BTN_REPEAT = 1, BTN_INFO = 2 };

	/// The local player's production buildings among the selected objects.
	void selectedFactories( std::vector<ObjectID> &out )
	{
		out.clear();
		if (TheInGameUI == nullptr)
			return;
		const DrawableList *list = TheInGameUI->getAllSelectedLocalDrawables();
		if (list == nullptr)
			return;
		for (DrawableListCIt it = list->begin(); it != list->end(); ++it)
		{
			const Object *obj = (*it)->getObject();
			if (obj && obj->isLocallyControlled() && PlayerAssist::isRepeatFactory( obj ))
				out.push_back( obj->getID() );
		}
	}

	/// Do all of them repeat?
	Bool allOn( const std::vector<ObjectID> &ids )
	{
		for (size_t i = 0; i < ids.size(); ++i)
		{
			if (!ThePlayerAssist->repeatOn( ids[i] ))
				return FALSE;
		}
		return !ids.empty();
	}

	void send( Int on, const std::vector<ObjectID> &ids )
	{
		GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_REPEAT_PRODUCTION );
		m->appendIntegerArgument( on );
		m->appendIntegerArgument( TheAssistOptions.m_repeatReserve );
		for (size_t i = 0; i < ids.size(); ++i)
			m->appendObjectIDArgument( ids[i] );
	}

	Bool usable()
	{
		return TheAssistOptions.m_repeatProduction && AssistUI::active() && ThePlayerAssist != nullptr;
	}

	/// The switch: on for all selected buildings, or off when all of them repeat already.
	void toggle()
	{
		if (!usable())
			return;
		std::vector<ObjectID> ids;
		selectedFactories( ids );
		if (ids.empty())
			return;
		send( allOn( ids ) ? 0 : 1, ids );
	}
}

//-------------------------------------------------------------------------------------------------
class RepeatPanel : public AssistPanel
{
public:
	RepeatPanel() : AssistPanel( "Repeat" )
	{
		m_title = L"Production";
		setStackLeft( TRUE );
		addButton( BTN_REPEAT, 0, L"Repeat", nullptr );
		addButton( BTN_INFO, 0, L"", nullptr );
		find( BTN_INFO )->m_flat = TRUE;
		find( BTN_INFO )->m_enabled = FALSE;
	}

	virtual Bool refresh() override
	{
		if (!usable())
			return FALSE;
		std::vector<ObjectID> ids;
		selectedFactories( ids );
		if (ids.empty())
			return FALSE;
		const Bool on = allOn( ids );
		AssistButton *b = find( BTN_REPEAT );
		b->m_on = on;
		b->m_label = on ? L"Repeat: on" : L"Repeat: off";
		UnicodeString tip( L"Build the last unit again whenever the queue runs empty" );
		const UnicodeString key = AssistUI::hotkeyText( GameMessage::MSG_META_ASSIST_REPEAT_PRODUCTION );
		if (!key.isEmpty())
			tip.format( L"Build the last unit again whenever the queue runs empty  (%ls)", key.str() );
		b->m_tip = tip;

		// what the first selected building that repeats does
		UnicodeString info;
		for (size_t i = 0; i < ids.size() && info.isEmpty(); ++i)
		{
			if (!ThePlayerAssist->repeatOn( ids[i] ))
				continue;
			const ThingTemplate *unit = ThePlayerAssist->repeatUnit( ids[i] );
			if (unit == nullptr)
				info = L"Queue a unit: it is built again and again";
			else if (ThePlayerAssist->repeatWaiting( ids[i] ))
				info.format( L"%ls: waiting (money or rules)", unit->getDisplayName().str() );
			else
				info.format( L"Repeats %ls", unit->getDisplayName().str() );
		}
		if (info.isEmpty())
			info.format( L"Money kept in reserve: %d", TheAssistOptions.m_repeatReserve );
		find( BTN_INFO )->m_label = info;
		m_title = L"Production";
		return TRUE;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		const Int gap = AssistUI::px( 3 );
		const Int titleH = AssistUI::px( 14 );
		AssistButton *b = find( BTN_REPEAT );
		AssistButton *info = find( BTN_INFO );
		b->m_x = gap;
		b->m_y = titleH + gap;
		b->m_w = AssistUI::px( 90 );
		b->m_h = AssistUI::px( 22 );
		info->m_x = b->m_x + b->m_w + AssistUI::px( 6 );
		info->m_y = b->m_y;
		info->m_w = AssistUI::px( 190 );
		info->m_h = b->m_h;
		*w = info->m_x + info->m_w + gap;
		*h = b->m_y + b->m_h + gap;
		*x = AssistUI::px( 6 );
		*y = AssistUI::stackY( this, *h );
	}

	virtual void clicked( Int id ) override
	{
		if (id == BTN_REPEAT)
			toggle();
	}

	virtual void drawGlyph( const AssistButton &b, Int x, Int y, Color color ) override
	{
		if (b.m_id == BTN_INFO)
		{
			AssistUI::text( b.m_label, x + b.m_x, y + b.m_y + (b.m_h - AssistUI::px( 12 )) / 2, GameMakeColor( 200, 215, 235, 255 ), 9 );
			return;
		}
		AssistPanel::drawGlyph( b, x, y, color );
	}
};

static RepeatPanel *s_panel = nullptr;

//-------------------------------------------------------------------------------------------------
void AssistRepeatUI::init()
{
	if (s_panel == nullptr)
		s_panel = new RepeatPanel;
}

//-------------------------------------------------------------------------------------------------
void AssistRepeatUI::reset()
{
}

//-------------------------------------------------------------------------------------------------
void AssistRepeatUI::reserveChanged()
{
	// the reserve is part of the rules the simulation follows: it goes out as a command
	if (usable())
		send( 2, std::vector<ObjectID>() );
}

//-------------------------------------------------------------------------------------------------
Bool AssistRepeatUI::translate( const GameMessage *msg )
{
	if (msg->getType() != GameMessage::MSG_META_ASSIST_REPEAT_PRODUCTION)
		return FALSE;
	if (!usable())
		return FALSE;
	toggle();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AssistRepeatUI::drawOverlays( View *view )
{
	if (!TheAssistOptions.m_repeatProduction || ThePlayerAssist == nullptr || TheTacticalView == nullptr)
		return;
	Player *local = ThePlayerList->getLocalPlayer();
	if (local == nullptr)
		return;
	std::vector<ObjectID> ids;
	ThePlayerAssist->repeatList( local->getPlayerIndex(), ids );
	for (size_t i = 0; i < ids.size(); ++i)
	{
		const Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr)
			continue;
		Coord3D p = *obj->getPosition();
		p.z += obj->getGeometryInfo().getMaxHeightAbovePosition() + 10.0f;
		ICoord2D s;
		if (!TheTacticalView->worldToScreen( &p, &s ))
			continue;
		const Bool waiting = ThePlayerAssist->repeatWaiting( ids[i] );
		const UnicodeString label( waiting ? L"REPEAT (waiting)" : L"REPEAT" );
		const Int w = AssistUI::textWidth( label, 9 ) + AssistUI::px( 8 );
		const Int h = AssistUI::px( 14 );
		const Color border = waiting ? GameMakeColor( 230, 190, 90, 255 ) : GameMakeColor( 120, 230, 150, 255 );
		AssistUI::box( s.x - w / 2, s.y - h, w, h, GameMakeColor( 12, 16, 24, 190 ), border );
		AssistUI::text( label, s.x - w / 2, s.y - h + AssistUI::px( 1 ), border, 9, TRUE, w );
	}
}
