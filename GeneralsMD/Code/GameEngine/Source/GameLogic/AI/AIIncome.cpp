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

// AIIncome.cpp
// Income that does not depend on the supply trucks, for the Expert computer player.
//
// The supply near the base runs out in a long game and the trucks are lost; a player who earns from units that make money (units
// with a HackInternetAIUpdate module: they hack the internet for cash) keeps an income to the end.  The scripts of the computer player
// train them, but not in every game.  The Expert:
//   * from IncomeStartSeconds on, keeps at least IncomeHackers such units: when it has fewer, an idle factory of ours that can make
//     one trains one (the money left after it at least SurplusReserve),
//   * sends the ones that stand idle to work: into a structure of ours that houses them (a contain module InternetHackContain) with
//     room, else to hack where they stand.
// Known by their modules, no names.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"

#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

/// Does the template have a behavior module of this name?
static Bool hasModule( const ThingTemplate *tt, const char *name )
{
	const ModuleInfo &modules = tt->getBehaviorModuleInfo();
	for (Int i = 0; i < modules.getCount(); ++i)
		if (modules.getNthName(i).compare(name) == 0)
			return TRUE;
	return FALSE;
}

void AIStrategy::updateIncome()
{
	const AISkillSettings &sk = skill();
	const UnsignedInt now = TheGameLogic->getFrame();
	if (sk.m_incomeHackers <= 0 || now < m_nextIncome || now < secondsToFrames(sk.m_incomeStartSeconds))
		return;
	m_nextIncome = now + 5 * LOGICFRAMES_PER_SECOND;

	// Our earners, and the houses for them.
	Int earners = 0;
	Object *house = nullptr;
	Object *idle[16];
	Int numIdle = 0;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (obj == nullptr || obj->isEffectivelyDead())
					continue;
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					ContainModuleInterface *contain = obj->getContain();
					if (house == nullptr && contain && !obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) && hasModule(obj->getTemplate(), "InternetHackContain") &&
							contain->getContainCount() < (UnsignedInt)contain->getContainMax())
						house = obj;
					continue;
				}
				AIUpdateInterface *ai = obj->getAI();
				HackInternetAIInterface *hack = ai ? ai->getHackInternetAIInterface() : nullptr;
				if (hack == nullptr)
					continue;
				++earners;
				if (!obj->isContained() && ai->isIdle() && !hack->isHacking() && !hack->isHackingPackingOrUnpacking() && numIdle < 16)
					idle[numIdle++] = obj;
			}
		}
	}
	// Earners inside a house are contained, and still counted above (they are members of their teams).
	for (Int i = 0; i < numIdle; ++i)
	{
		if (house && house->getContain()->getContainCount() < (UnsignedInt)house->getContain()->getContainMax())
			idle[i]->getAI()->aiEnter(house, CMD_FROM_AI);
		else
			idle[i]->getAI()->aiHackInternet(CMD_FROM_AI);
		++m_incomeOrders;
	}
	if (earners >= sk.m_incomeHackers || TheControlBar == nullptr)
		return;

	// Too few: an idle factory of ours that can make one trains one.
	const Real money = (Real)m_player->getMoney()->countMoney();
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *factory = oit.cur();
				if (factory == nullptr || factory->isEffectivelyDead() || !factory->isKindOf(KINDOF_STRUCTURE) || factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					continue;
				ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
				const CommandSet *set = pu ? TheControlBar->findCommandSet(factory->getCommandSetString()) : nullptr;
				if (set == nullptr || pu->getProductionCount() > 0)
					continue;
				for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
				{
					const CommandButton *button = set->getCommandButton(i);
					const ThingTemplate *tmpl = button && button->getCommandType() == GUI_COMMAND_UNIT_BUILD ? button->getThingTemplate() : nullptr;
					if (tmpl == nullptr || !hasModule(tmpl, "HackInternetAIUpdate"))
						continue;
					if (money - (Real)tmpl->calcCostToBuild(m_player) < sk.m_surplusReserve || TheBuildAssistant->canMakeUnit(factory, tmpl) != CANMAKE_OK)
						continue;
					if (pu->queueCreateUnit(tmpl, pu->requestUniqueUnitID()))
					{
						++m_incomeTrained;
						AI_TRACE("INCOME: %d earner(s) of %d wanted: %s ordered at %s %u", earners, sk.m_incomeHackers, tmpl->getName().str(), factory->getTemplate()->getName().str(), factory->getID());
						return;
					}
				}
			}
		}
	}
}
