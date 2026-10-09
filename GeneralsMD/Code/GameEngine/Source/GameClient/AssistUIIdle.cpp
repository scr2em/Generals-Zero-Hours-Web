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

// AssistUIIdle.cpp
// The idle hotkeys: one key selects the next idle army unit and centres the view on it, one selects all idle army units,
// one selects the next idle worker (builders and supply gatherers).  A small counter at the top right shows how many of
// each stand idle; its buttons do what the keys do.  Selection only: no orders, so they work in every match.  Which units
// are idle is decided in GameLogic/AssistIdle.cpp (the army units are those of the "base under attack" response).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
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
	enum { BTN_ARMY = 1, BTN_ALL = 2, BTN_WORKERS = 3 };

	ObjectID		s_lastPicked[3] = { INVALID_ID, INVALID_ID, INVALID_ID };	// per PlayerAssist::IdlePick
	Int					s_army = 0;
	Int					s_workers = 0;
	UnsignedInt	s_countFrame = 0xffffffff;
	Int					s_loggedArmy = -1;
	Int					s_loggedWorkers = -1;

	Bool enabled()
	{
		return TheAssistOptions.m_idleKeys && AssistUI::playing();
	}

	/// Count the idle units, at most every few frames (it walks the player's objects).
	void count()
	{
		const UnsignedInt now = TheGameLogic->getFrame();
		if (s_countFrame != 0xffffffff && now >= s_countFrame && now - s_countFrame < 10)
			return;
		s_countFrame = now;
		std::vector<Object *> list;
		const Player *local = ThePlayerList->getLocalPlayer();
		PlayerAssist::idleUnits( local, PlayerAssist::IDLE_ARMY, list );
		s_army = (Int)list.size();
		PlayerAssist::idleUnits( local, PlayerAssist::IDLE_WORKER, list );
		s_workers = (Int)list.size();
		if (s_army != s_loggedArmy || s_workers != s_loggedWorkers)
		{
			s_loggedArmy = s_army;
			s_loggedWorkers = s_workers;
			ASSIST_DEBUG(( "ASSIST idle count army=%d workers=%d", s_army, s_workers ));
		}
	}

	/// Select what the hotkey picks, as a click would: the drawables and the selection message to the logic.
	void pick( Int which )
	{
		if (!enabled() || TheInGameUI == nullptr)
			return;
		std::vector<ObjectID> ids;
		PlayerAssist::pickIdle( ThePlayerList->getLocalPlayer(), which, s_lastPicked[which], ids );
		if (ids.empty())
			return;

		TheInGameUI->deselectAllDrawables();
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
		msg->appendBooleanArgument( TRUE );
		Object *first = nullptr;
		for (size_t i = 0; i < ids.size(); ++i)
		{
			Object *obj = TheGameLogic->findObjectByID( ids[i] );
			if (obj == nullptr)
				continue;
			if (first == nullptr)
				first = obj;
			msg->appendObjectIDArgument( ids[i] );
			if (obj->getDrawable())
				TheInGameUI->selectDrawable( obj->getDrawable() );
		}
		if (first && which != PlayerAssist::IDLE_PICK_ARMY_ALL)
		{
			s_lastPicked[which] = first->getID();
			if (TheTacticalView)
				TheTacticalView->userLookAt( first->getPosition() );
		}
		AssistUI::invalidateSelection();
	}
}

//-------------------------------------------------------------------------------------------------
class IdlePanel : public AssistPanel
{
public:
	IdlePanel() : AssistPanel( "Idle" )
	{
		m_title = L"Idle";
		addButton( BTN_ARMY, 0, L"Army", nullptr );
		addButton( BTN_ALL, 0, L"All", nullptr );
		addButton( BTN_WORKERS, 0, L"Workers", nullptr );
	}

	virtual Bool refresh() override
	{
		if (!enabled())
			return FALSE;
		count();
		AssistButton *army = find( BTN_ARMY );
		army->m_label.format( L"Army %d", s_army );
		army->m_enabled = s_army > 0;
		army->m_tip = tip( L"Next idle army unit", GameMessage::MSG_META_ASSIST_IDLE_ARMY_NEXT );
		AssistButton *all = find( BTN_ALL );
		all->m_enabled = s_army > 0;
		all->m_tip = tip( L"All idle army units", GameMessage::MSG_META_ASSIST_IDLE_ARMY_ALL );
		AssistButton *workers = find( BTN_WORKERS );
		workers->m_label.format( L"Workers %d", s_workers );
		workers->m_enabled = s_workers > 0;
		workers->m_tip = tip( L"Next idle worker", GameMessage::MSG_META_ASSIST_IDLE_WORKER_NEXT );
		return TRUE;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		flow( 3, AssistUI::px( 60 ), AssistUI::px( 20 ), AssistUI::px( 3 ), w, h );
		find( BTN_ALL )->m_w = AssistUI::px( 34 );
		find( BTN_WORKERS )->m_x = find( BTN_ALL )->m_x + find( BTN_ALL )->m_w + AssistUI::px( 3 );
		find( BTN_WORKERS )->m_w = AssistUI::px( 72 );
		*w = find( BTN_WORKERS )->m_x + find( BTN_WORKERS )->m_w + AssistUI::px( 3 );
		*x = (Int)TheDisplay->getWidth() - *w - AssistUI::px( 6 );
		*y = AssistUI::belowToolbar();
	}

	virtual void clicked( Int id ) override
	{
		if (id == BTN_ARMY)
			pick( PlayerAssist::IDLE_PICK_ARMY_NEXT );
		else if (id == BTN_ALL)
			pick( PlayerAssist::IDLE_PICK_ARMY_ALL );
		else
			pick( PlayerAssist::IDLE_PICK_WORKER_NEXT );
	}

private:
	static UnicodeString tip( const wchar_t *what, GameMessage::Type key )
	{
		UnicodeString out( what );
		const UnicodeString k = AssistUI::hotkeyText( key );
		if (!k.isEmpty())
			out.format( L"%ls  (%ls)", what, k.str() );
		return out;
	}
};

static IdlePanel *s_panel = nullptr;

//-------------------------------------------------------------------------------------------------
void AssistIdleUI::init()
{
	if (s_panel == nullptr)
		s_panel = new IdlePanel;
}

//-------------------------------------------------------------------------------------------------
void AssistIdleUI::reset()
{
	for (Int i = 0; i < 3; ++i)
		s_lastPicked[i] = INVALID_ID;
	s_countFrame = 0xffffffff;
	s_loggedArmy = -1;
	s_loggedWorkers = -1;
}

//-------------------------------------------------------------------------------------------------
Bool AssistIdleUI::translate( const GameMessage *msg )
{
	Int which = -1;
	switch (msg->getType())
	{
		case GameMessage::MSG_META_ASSIST_IDLE_ARMY_NEXT:		which = PlayerAssist::IDLE_PICK_ARMY_NEXT; break;
		case GameMessage::MSG_META_ASSIST_IDLE_ARMY_ALL:		which = PlayerAssist::IDLE_PICK_ARMY_ALL; break;
		case GameMessage::MSG_META_ASSIST_IDLE_WORKER_NEXT:	which = PlayerAssist::IDLE_PICK_WORKER_NEXT; break;
		default: return FALSE;
	}
	if (!enabled())
		return FALSE;
	pick( which );
	return TRUE;
}
