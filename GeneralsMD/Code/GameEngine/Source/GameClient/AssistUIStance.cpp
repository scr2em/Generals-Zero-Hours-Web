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

// AssistUIStance.cpp
// The client side of the unit stances: a small panel above the command bar with a switch for each stance (kite, retreat
// when damaged, spread out, split fire) for the selected units, and the hotkeys.  The orders go out as MSG_ASSIST_STANCE;
// what the stances do is in the simulation (GameLogic/AssistStance.cpp).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/MessageStream.h"
#include "GameClient/AssistUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"

namespace
{
	enum { BTN_KITE = 1, BTN_RETREAT = 2, BTN_SPREAD = 3, BTN_SPLIT = 4 };
	const Int RETREAT_STEPS[] = { 0, 30, 50, 70 };

	/// What the selected units share: the stance bits, and the retreat setting.
	Int sharedState( Int *percent, Bool *any )
	{
		const AssistUI::Selection &sel = AssistUI::selection();
		Int p = 0;
		Bool found = FALSE;
		for (size_t i = 0; i < sel.m_ids.size(); ++i)
			found = found || PlayerAssist::stanceEligible( TheGameLogic->findObjectByID( sel.m_ids[i] ) );
		const Int flags = found ? ThePlayerAssist->sharedStance( sel.m_ids, &p ) : 0;
		if (percent)
			*percent = p;
		if (any)
			*any = found;
		return flags;
	}

	void sendStance( Int mask, Int value, Int percent )
	{
		GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_STANCE );
		m->appendIntegerArgument( mask );
		m->appendIntegerArgument( value );
		m->appendIntegerArgument( percent );
	}

	/// A switch was pressed (or its hotkey): turn the stance on for all selected units, or off when all have it.
	void toggle( Int bit )
	{
		if (!TheAssistOptions.m_stances || !AssistUI::active())
			return;
		Int percent = 0;
		Bool any = FALSE;
		const Int flags = sharedState( &percent, &any );
		if (!any)
			return;
		if (bit == STANCE_RETREAT)
		{
			// off, 30, 50, 70 percent of the health
			Int next = 0;
			for (Int i = 0; i < 4; ++i)
			{
				if (RETREAT_STEPS[i] == percent)
					next = (i + 1) % 4;
			}
			if ((flags & STANCE_RETREAT) == 0)
				next = 1;
			sendStance( STANCE_RETREAT, RETREAT_STEPS[next] > 0 ? STANCE_RETREAT : 0, RETREAT_STEPS[next] );
			return;
		}
		sendStance( bit, (flags & bit) ? 0 : bit, 0 );
	}
}

//-------------------------------------------------------------------------------------------------
class StancePanel : public AssistPanel
{
public:
	StancePanel() : AssistPanel( "Stance" )
	{
		m_title = L"Stance";
		setStackLeft( TRUE );
		addButton( BTN_KITE, 0, L"Kite", L"Step back while the weapon reloads (Alt+K)" );
		addButton( BTN_RETREAT, 0, L"Retreat", L"Pull out when damaged: off, 30, 50, 70 percent (Alt+R)" );
		addButton( BTN_SPREAD, 0, L"Spread", L"Keep apart against area weapons (Alt+S)" );
		addButton( BTN_SPLIT, 0, L"Split", L"Do not pile on a target that is dying: split the fire (Alt+X)" );
	}

	virtual Bool refresh() override
	{
		if (!TheAssistOptions.m_stances || !AssistUI::active())
			return FALSE;
		Int percent = 0;
		Bool any = FALSE;
		const Int flags = sharedState( &percent, &any );
		if (!any)
			return FALSE;
		find( BTN_KITE )->m_on = (flags & STANCE_KITE) != 0;
		find( BTN_SPREAD )->m_on = (flags & STANCE_SPREAD) != 0;
		find( BTN_SPLIT )->m_on = (flags & STANCE_SPLIT) != 0;
		AssistButton *r = find( BTN_RETREAT );
		r->m_on = (flags & STANCE_RETREAT) != 0;
		if (r->m_on)
			r->m_label.format( L"Retreat %d%%", percent );
		else
			r->m_label = L"Retreat";
		m_title = L"Stance";
		return TRUE;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		flow( 4, AssistUI::px( 62 ), AssistUI::px( 22 ), AssistUI::px( 3 ), w, h );
		*x = AssistUI::px( 6 );
		*y = AssistUI::stackY( this, *h );
	}

	virtual void clicked( Int id ) override
	{
		switch (id)
		{
			case BTN_KITE:		toggle( STANCE_KITE ); break;
			case BTN_RETREAT:	toggle( STANCE_RETREAT ); break;
			case BTN_SPREAD:	toggle( STANCE_SPREAD ); break;
			case BTN_SPLIT:		toggle( STANCE_SPLIT ); break;
		}
	}
};

static StancePanel *s_panel = nullptr;

//-------------------------------------------------------------------------------------------------
void AssistStanceUI::init()
{
	if (s_panel == nullptr)
		s_panel = new StancePanel;
}

//-------------------------------------------------------------------------------------------------
void AssistStanceUI::reset()
{
}

//-------------------------------------------------------------------------------------------------
Bool AssistStanceUI::translate( const GameMessage *msg )
{
	switch (msg->getType())
	{
		case GameMessage::MSG_META_ASSIST_STANCE_KITE:		toggle( STANCE_KITE ); return TRUE;
		case GameMessage::MSG_META_ASSIST_STANCE_RETREAT:	toggle( STANCE_RETREAT ); return TRUE;
		case GameMessage::MSG_META_ASSIST_STANCE_SPREAD:	toggle( STANCE_SPREAD ); return TRUE;
		case GameMessage::MSG_META_ASSIST_STANCE_SPLIT:		toggle( STANCE_SPLIT ); return TRUE;
		default: return FALSE;
	}
}
