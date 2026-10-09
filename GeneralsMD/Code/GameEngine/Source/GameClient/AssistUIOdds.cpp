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

// AssistUIOdds.cpp
// The odds meter (display only): with own armed units selected, the mouse over an enemy that the player can see
// gets a verdict on a fight between the selection and the enemy group around it (every enemy the player can see
// within reach of it): favourable, even or unfavourable.
//
// The estimate is the pair-wise combat model of the Expert computer player (AICombatModel: weapon damage types
// against armor tables, health, cost), not unit names.  It uses only what the player can see: the units on the
// screen, their type and their current health; nothing under the shroud and no stealthed unit that is not detected.
// Lanchester's linear law: the side that kills the other faster (its damage per second against the mix of the
// opponent, over the opponent's health) wins; the ratio of the two kill rates is the verdict.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>
#include <stdio.h>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

namespace
{
	Bool s_on = FALSE;
	UnsignedInt s_lastProbe = 0;

	struct Fighter
	{
		const AICombatFigures	*m_fig;
		Real									m_health;
		Real									m_cost;
	};

	struct Verdict
	{
		Bool		m_valid;
		Int			m_kind;			// -1 unfavourable, 0 even, 1 favourable, 2 cannot fight
		Real		m_ratio;
		Int			m_ownCount, m_enemyCount;
		Real		m_ownCost, m_enemyCost;
		ObjectID	m_hovered;
		UnsignedInt	m_frame;
	};
	Verdict s_verdict;
	UnsignedInt s_lastDebug = 0;

	void addFighter( std::vector<Fighter> &side, const Object *obj, Player *costPlayer )
	{
		Fighter f;
		f.m_fig = AICombatModel::figures( obj->getTemplate() );
		f.m_health = obj->getBodyModule() ? obj->getBodyModule()->getHealth() : 0.0f;
		f.m_cost = (Real)obj->getTemplate()->calcCostToBuild( costPlayer );
		if (f.m_fig && f.m_health > 0.0f)
			side.push_back( f );
	}

	/// The damage per second the side does to the other, averaged over the other's health.
	Real sideDps( const std::vector<Fighter> &attackers, const std::vector<Fighter> &defenders, Real defenderHealth )
	{
		if (defenderHealth <= 0.0f)
			return 0.0f;
		Real total = 0.0f;
		for (size_t a = 0; a < attackers.size(); ++a)
		{
			if (attackers[a].m_fig == nullptr || !attackers[a].m_fig->m_armed)
				continue;
			Real mix = 0.0f;
			for (size_t d = 0; d < defenders.size(); ++d)
				mix += defenders[d].m_health / defenderHealth * AICombatModel::damagePerSecond( attackers[a].m_fig, defenders[d].m_fig );
			total += mix;
		}
		return total;
	}

	Real totalHealth( const std::vector<Fighter> &side )
	{
		Real h = 0.0f;
		for (size_t i = 0; i < side.size(); ++i)
			h += side[i].m_health;
		return h;
	}

	/// An enemy the local player sees: not under the shroud, not stealthed without being detected.
	Bool visibleToLocal( const Object *obj, Int localIndex )
	{
		if (obj->isEffectivelyDead() || obj->isOffMap())
			return FALSE;
		if (obj->getShroudedStatus( localIndex ) != OBJECTSHROUD_CLEAR)
			return FALSE;
		if (obj->testStatus( OBJECT_STATUS_STEALTHED ) && !obj->testStatus( OBJECT_STATUS_DETECTED ))
			return FALSE;
		return TRUE;
	}

	Bool compute( Object *hovered, Player *local, const std::vector<ObjectID> &selected, Verdict *out )
	{
		std::vector<Fighter> own, enemy;
		for (size_t i = 0; i < selected.size(); ++i)
		{
			const Object *o = TheGameLogic->findObjectByID( selected[i] );
			if (o)
				addFighter( own, o, local );
		}

		// the enemies around the hovered one that the player can see
		{
			PartitionFilterAlive alive;
			PartitionFilterRelationship sameSide( hovered, PartitionFilterRelationship::ALLOW_ALLIES );
			PartitionFilter *filters[] = { &sameSide, &alive, nullptr };
			SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( hovered->getPosition(), 220.0f, FROM_CENTER_2D, filters );
			MemoryPoolObjectHolder hold( iter );
			for (Object *o = iter->first(); o; o = iter->next())
			{
				if (local->getRelationship( o->getTeam() ) == ENEMIES && visibleToLocal( o, local->getPlayerIndex() ) && !o->isKindOf( KINDOF_UNATTACKABLE ))
					addFighter( enemy, o, local );
			}
		}
		if (own.empty() || enemy.empty())
			return FALSE;

		Verdict v;
		v.m_valid = TRUE;
		v.m_ownCount = (Int)own.size();
		v.m_enemyCount = (Int)enemy.size();
		v.m_ownCost = v.m_enemyCost = 0.0f;
		for (size_t i = 0; i < own.size(); ++i)
			v.m_ownCost += own[i].m_cost;
		for (size_t i = 0; i < enemy.size(); ++i)
			v.m_enemyCost += enemy[i].m_cost;

		const Real ownHealth = totalHealth( own );
		const Real enemyHealth = totalHealth( enemy );
		const Real ours = sideDps( own, enemy, enemyHealth ) / enemyHealth;			// the fraction of the enemy we destroy per second
		const Real theirs = sideDps( enemy, own, ownHealth ) / ownHealth;				// the fraction of ours they destroy per second
		v.m_hovered = hovered->getID();
		v.m_frame = TheGameLogic->getFrame();
		if (ours <= 0.0f)
		{
			v.m_kind = 2;
			v.m_ratio = 0.0f;
		}
		else
		{
			v.m_ratio = theirs > 0.0f ? ours / theirs : 99.0f;
			v.m_kind = v.m_ratio >= 1.4f ? 1 : (v.m_ratio >= 0.7f ? 0 : -1);
		}
		*out = v;
		return TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool oddsIsOn() { return s_on; }
static void oddsToggle() { AssistOddsUI::toggle(); }

//-------------------------------------------------------------------------------------------------
void AssistOddsUI::init()
{
	AssistUI::ToolbarEntry e;
	e.m_id = 2;
	e.m_label = L"Odds";
	e.m_tip = L"Odds meter: point at an enemy group with units selected";
	e.m_option = &TheAssistOptions.m_odds;
	e.m_isOn = &oddsIsOn;
	e.m_toggle = &oddsToggle;
	AssistUI::addToolbarEntry( e );
}

//-------------------------------------------------------------------------------------------------
void AssistOddsUI::reset()
{
	s_on = FALSE;
	s_verdict.m_valid = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AssistOddsUI::toggle()
{
	s_on = !s_on;
	ASSIST_DEBUG(( "ASSIST odds view %s", s_on ? "on" : "off" ));
}

//-------------------------------------------------------------------------------------------------
Bool AssistOddsUI::translate( const GameMessage *msg )
{
	if (msg->getType() != GameMessage::MSG_META_ASSIST_ODDS)
		return FALSE;
	if (!TheAssistOptions.m_odds || !AssistUI::playing())
		return FALSE;
	toggle();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AssistOddsUI::drawOverlays( View *view )
{
	if (!s_on || !TheAssistOptions.m_odds || TheTacticalView == nullptr || TheMouse == nullptr)
		return;
	Player *local = ThePlayerList->getLocalPlayer();
	if (local == nullptr)
		return;

	// own armed units selected?
	const AssistUI::Selection &sel = AssistUI::selection();
	std::vector<ObjectID> armed;
	const DrawableList *list = TheInGameUI->getAllSelectedLocalDrawables();
	if (list)
	{
		for (DrawableListCIt it = list->begin(); it != list->end(); ++it)
		{
			const Object *o = (*it)->getObject();
			if (o == nullptr || !o->isLocallyControlled())
				continue;
			const AICombatFigures *f = AICombatModel::figures( o->getTemplate() );
			if (f && f->m_armed)
				armed.push_back( o->getID() );
		}
	}
	(void)sel;
	if (armed.empty())
		return;

	// the enemy under the mouse
	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	Drawable *d = TheTacticalView->pickDrawable( &mouse, FALSE, PICK_TYPE_SELECTABLE );
	Object *hovered = d ? d->getObject() : nullptr;
	if (TheAssistOptions.m_debug && TheGameLogic->getFrame() - s_lastProbe > 60)
	{
		s_lastProbe = TheGameLogic->getFrame();
		ASSIST_DEBUG(( "ASSIST odds probe armed=%d mouse=%d,%d hovered=%d", (int)armed.size(), (int)mouse.x, (int)mouse.y, hovered ? (int)hovered->getID() : 0 ));
	}
	if (hovered == nullptr || local->getRelationship( hovered->getTeam() ) != ENEMIES || !visibleToLocal( hovered, local->getPlayerIndex() ))
	{
		s_verdict.m_valid = FALSE;
		return;
	}

	// recompute a few times a second
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!s_verdict.m_valid || s_verdict.m_hovered != hovered->getID() || now - s_verdict.m_frame > 8)
	{
		Verdict v;
		if (!compute( hovered, local, armed, &v ))
		{
			s_verdict.m_valid = FALSE;
			return;
		}
		s_verdict = v;
		if (TheAssistOptions.m_debug && now - s_lastDebug > 30)
		{
			s_lastDebug = now;
			ASSIST_DEBUG(( "ASSIST odds kind=%d ratio=%.2f own=%d enemy=%d ownCost=%.0f enemyCost=%.0f", v.m_kind, v.m_ratio, v.m_ownCount, v.m_enemyCount, v.m_ownCost, v.m_enemyCost ));
		}
	}

	UnicodeString head, detail;
	Color color;
	switch (s_verdict.m_kind)
	{
		case 1:  head.format( L"Favourable  x%.1f", s_verdict.m_ratio > 50.0f ? 50.0f : s_verdict.m_ratio ); color = GameMakeColor( 110, 255, 130, 255 ); break;
		case 0:  head.format( L"Even  x%.1f", s_verdict.m_ratio ); color = GameMakeColor( 255, 235, 110, 255 ); break;
		case -1: head.format( L"Unfavourable  x%.1f", s_verdict.m_ratio ); color = GameMakeColor( 255, 110, 100, 255 ); break;
		default: head = L"Cannot hurt them"; color = GameMakeColor( 255, 110, 100, 255 ); break;
	}
	detail.format( L"you %d ($%d)  vs  them %d ($%d)", s_verdict.m_ownCount, (Int)s_verdict.m_ownCost, s_verdict.m_enemyCount, (Int)s_verdict.m_enemyCost );

	const Int w1 = AssistUI::textWidth( head, 12 );
	const Int w2 = AssistUI::textWidth( detail, 9 );
	const Int w = (w1 > w2 ? w1 : w2) + AssistUI::px( 14 );
	const Int h = AssistUI::px( 36 );
	Int x = mouse.x + AssistUI::px( 18 );
	Int y = mouse.y + AssistUI::px( 18 );
	if (x + w > (Int)TheDisplay->getWidth())
		x = mouse.x - w - AssistUI::px( 10 );
	if (y + h > (Int)TheDisplay->getHeight())
		y = mouse.y - h - AssistUI::px( 10 );
	AssistUI::box( x, y, w, h, GameMakeColor( 8, 12, 20, 225 ), color );
	AssistUI::text( head, x + AssistUI::px( 7 ), y + AssistUI::px( 3 ), color, 12 );
	AssistUI::text( detail, x + AssistUI::px( 7 ), y + AssistUI::px( 20 ), GameMakeColor( 220, 230, 245, 255 ), 9 );
}
