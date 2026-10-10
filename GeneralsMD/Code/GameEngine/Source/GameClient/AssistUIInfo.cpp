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

// AssistUIInfo.cpp
// The info strip: a slim line at the top of the screen with the local player's income per minute, the value of the army,
// the game time and the actions per minute.  Display only, and only what the player knows anyway: the player's own money,
// the player's own units, the logic frame and the player's own commands.
//
//   income   the game's own cash per minute of the player (Money::getCashPerMinute): money from supplies, oil derricks,
//            bounties ... in the last minute of game time; not refunds or sales
//   army     the build cost of the player's army units (armed, not workers or support, not structures; also those inside
//            transports and buildings)
//   time     game time (logic frames)
//   APM      the player's commands that reached the game in the last minute of game time (orders, selections, production;
//            not the camera); less than a minute into the match, scaled up from at least ten seconds

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <deque>

#include "Common/AssistOptions.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/AssistHooks.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

namespace
{
	const UnsignedInt MINUTE = 60 * LOGICFRAMES_PER_SECOND;

	Bool										s_shown = TRUE;		// the toolbar button hides it
	std::deque<UnsignedInt>	s_actions;				// logic frames of the player's commands
	UnsignedInt							s_lastFrame = 0xffffffff;
	UnsignedInt							s_lastLog = 0;
	Int											s_armyValue = 0;
	UnsignedInt							s_armyFrame = 0xffffffff;

	Bool infoIsOn() { return s_shown; }
	void infoToggle() { s_shown = !s_shown; }

	/// A command of a player reached the game (a hook called from GameLogic::logicMessageDispatcher).
	void commandHook( const GameMessage *msg )
	{
		if (!TheAssistOptions.m_infoStrip || ThePlayerList == nullptr || TheGameLogic == nullptr)
			return;
		const Player *local = ThePlayerList->getLocalPlayer();
		if (local == nullptr || msg->getPlayerIndex() != local->getPlayerIndex())
			return;
		if (msg->getType() == GameMessage::MSG_CLEAR_INGAME_POPUP_MESSAGE)
			return;
		s_actions.push_back( TheGameLogic->getFrame() );
	}

	/// Army units for their value: those of PlayerAssist::isArmyUnit, and also those inside a transport or a building.
	void addArmy( Object *obj, void *userData )
	{
		if (obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) ||
				obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->getAIUpdateInterface() == nullptr)
			return;
		const AICombatFigures *f = AICombatModel::figures( obj->getTemplate() );
		if (f == nullptr || !f->m_armed || f->m_supportLevel != 0 || !(f->m_canHitGround || f->m_canHitAir))
			return;
		*(Int *)userData += obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() );
	}

	void sample( Player *local )
	{
		const UnsignedInt now = TheGameLogic->getFrame();
		if (s_lastFrame != 0xffffffff && now < s_lastFrame)
		{
			// a new match (or a loaded game)
			s_actions.clear();
			s_armyFrame = 0xffffffff;
			s_lastLog = 0;
		}
		s_lastFrame = now;
		while (!s_actions.empty() && now - s_actions.front() >= MINUTE)
			s_actions.pop_front();
		if (s_armyFrame == 0xffffffff || now < s_armyFrame || now - s_armyFrame >= LOGICFRAMES_PER_SECOND / 2)
		{
			s_armyValue = 0;
			local->iterateObjects( addArmy, &s_armyValue );
			s_armyFrame = now;
		}
	}

	/// Actions per minute: the commands of the last minute (or of the time there was, at least ten seconds, scaled to a minute).
	Int actionsPerMinute()
	{
		const UnsignedInt now = TheGameLogic->getFrame();
		const UnsignedInt least = 10 * LOGICFRAMES_PER_SECOND;
		const UnsignedInt span = now >= MINUTE ? MINUTE : (now > least ? now : least);
		return (Int)((Real)s_actions.size() * (Real)MINUTE / (Real)span + 0.5f);
	}

	UnicodeString gameTime()
	{
		const UnsignedInt seconds = TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND;
		UnicodeString t;
		if (seconds >= 3600)
			t.format( L"%u:%02u:%02u", seconds / 3600, (seconds / 60) % 60, seconds % 60 );
		else
			t.format( L"%u:%02u", seconds / 60, seconds % 60 );
		return t;
	}
}

//-------------------------------------------------------------------------------------------------
void AssistInfoUI::init()
{
	TheAssistCommandHook = &commandHook;
	AssistUI::ToolbarEntry e;
	e.m_id = 3;
	e.m_label = L"Info";
	e.m_tip = L"Info strip: income per minute, army value, game time, actions per minute";
	e.m_option = &TheAssistOptions.m_infoStrip;
	e.m_isOn = &infoIsOn;
	e.m_toggle = &infoToggle;
	AssistUI::addToolbarEntry( e );
}

//-------------------------------------------------------------------------------------------------
void AssistInfoUI::reset()
{
	s_actions.clear();
	s_lastFrame = 0xffffffff;
	s_armyFrame = 0xffffffff;
	s_lastLog = 0;
}

//-------------------------------------------------------------------------------------------------
void AssistInfoUI::drawOverlays( View *view )
{
	Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if (!TheAssistOptions.m_infoStrip || local == nullptr)
		return;
	sample( local );
	if (!s_shown)
		return;

	const Int income = (Int)local->getMoney()->getCashPerMinute();
	const Int apm = actionsPerMinute();
	const UnicodeString time = gameTime();

	const UnsignedInt now = TheGameLogic->getFrame();
	if (s_lastLog == 0 || now >= s_lastLog + 10 * LOGICFRAMES_PER_SECOND)
	{
		s_lastLog = now > 0 ? now : 1;
		AsciiString t;
		t.translate( time );
		ASSIST_DEBUG(( "ASSIST info income=%d/min army=%d time=%s apm=%d", income, s_armyValue, t.str(), apm ));
	}

	// four fields of a fixed width, centred at the top of the screen
	const Int fieldW = AssistUI::px( 104 );
	const Int h = AssistUI::px( 16 );
	const Int w = 4 * fieldW;
	const Int x = ((Int)TheDisplay->getWidth() - w) / 2;
	const Int y = AssistUI::px( 3 );
	AssistUI::box( x, y, w, h, GameMakeColor( 12, 16, 24, 170 ), GameMakeColor( 120, 150, 190, 200 ) );

	UnicodeString f[4];
	f[0].format( L"Income %d/min", income );
	f[1].format( L"Army %d", s_armyValue );
	f[2].format( L"Time %ls", time.str() );
	f[3].format( L"APM %d", apm );
	const Color colors[4] =
	{
		GameMakeColor( 140, 230, 140, 255 ), GameMakeColor( 255, 200, 120, 255 ), GameMakeColor( 220, 225, 235, 255 ), GameMakeColor( 150, 200, 255, 255 )
	};
	for (Int i = 0; i < 4; ++i)
	{
		if (i > 0)
			AssistUI::line( x + i * fieldW, y + AssistUI::px( 3 ), x + i * fieldW, y + h - AssistUI::px( 3 ), 1, GameMakeColor( 90, 110, 140, 200 ) );
		AssistUI::text( f[i], x + i * fieldW, y + (h - AssistUI::px( 12 )) / 2, colors[i], 9, TRUE, fieldW );
	}
}
