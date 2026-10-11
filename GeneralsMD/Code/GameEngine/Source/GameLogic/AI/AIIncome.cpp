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
//   * keeps at least IncomeBuildings structures that bring money by themselves (KindOf FS_SUPPLY_DROPZONE: the supply drop zone;
//     FS_BLACK_MARKET: the black market), when its workers can build one: one at a time goes on the build list, on the side of the
//     base away from the enemy.  (The surplus production keeps back the money for the build list's next structure.)  One that is
//     destroyed is replaced, IncomeRebuilds times at most (an enemy that keeps destroying them does not keep us poor).
// Known by their modules and kinds, no names.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AutoDepositUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/TerrainVisual.h"

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
	if ((sk.m_incomeHackers <= 0 && sk.m_incomeRate <= 0.0f) || now < m_nextIncome || now < secondsToFrames(sk.m_incomeStartSeconds))
		return;
	m_nextIncome = now + 5 * LOGICFRAMES_PER_SECOND;
	updateIncomeBuildings();
	if (sk.m_incomeHackers <= 0)
		return;

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

/// Is this structure one that brings money by itself?
static Bool isIncomeStructure( const ThingTemplate *tt )
{
	return tt && tt->isKindOf(KINDOF_STRUCTURE) && (tt->isKindOf(KINDOF_FS_SUPPLY_DROPZONE) || tt->isKindOf(KINDOF_FS_BLACK_MARKET));
}

/// What one of them brings in a minute: read from its deposit module (the black market), else (the supply drop zone, whose money
/// comes with the drops of an object creation list) the setting IncomeDropZoneRate.
static Real incomePerMinute( const ThingTemplate *tt, const AISkillSettings &sk )
{
	const ModuleInfo &modules = tt->getBehaviorModuleInfo();
	for (Int i = 0; i < modules.getCount(); ++i)
	{
		if (modules.getNthName(i).compare("AutoDepositUpdate") != 0)
			continue;
		const AutoDepositUpdateModuleData *d = static_cast<const AutoDepositUpdateModuleData *>(modules.getNthData(i));
		if (d && d->m_depositFrame > 0 && d->m_depositAmount > 0)
			return (Real)d->m_depositAmount * 60.0f * LOGICFRAMES_PER_SECOND / (Real)d->m_depositFrame;
	}
	return sk.m_incomeDropZoneRate;
}

/// Our build list's entry of this kind that waits to be built (none: nullptr).  `lost`: our entries of it that were built and are
/// gone (they are built once).
static BuildListInfo *waitingEntry( Player *player, const ThingTemplate *tt, Int *lost )
{
	if (lost)
		*lost = 0;
	BuildListInfo *waiting = nullptr;
	for (BuildListInfo *info = player->getBuildList(); info; info = info->getNext())
	{
		if (info->getTemplateName() != tt->getName() || TheGameLogic->findObjectByID(info->getObjectID()) != nullptr)
			continue;
		if (info->isBuildable())
		{
			if (waiting == nullptr)
				waiting = info;
		}
		else if (lost)
			++*lost;
	}
	return waiting;
}

/// A place for one more structure of ours around the base, on the side away from the enemy first, that the army can walk to.
Bool AIStrategy::incomeSite( const ThingTemplate *plan, Coord3D *where )
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return FALSE;
	Coord3D enemy;
	Real away = 0.0f;
	if (enemyStartPosition(&enemy))
		away = atan2f(base.y - enemy.y, base.x - enemy.x);
	const Real radius = m_ai->m_baseRadius > 100.0f ? m_ai->m_baseRadius : 300.0f;
	static const Real rings[4] = { 0.7f, 1.0f, 1.3f, 1.6f };
	for (Int r = 0; r < 4; ++r)
	{
		for (Int k = 0; k < 11; ++k)
		{
			const Real turn = (k == 0 ? 0.0f : ((k + 1) / 2) * (PI / 6.0f) * ((k & 1) ? 1.0f : -1.0f));
			Coord3D pos;
			pos.x = base.x + radius * rings[r] * cosf(away + turn);
			pos.y = base.y + radius * rings[r] * sinf(away + turn);
			pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
			const Bool legal = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&pos, plan, plan->getPlacementViewAngle(),
				BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player);
			TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild leaves bib feedback
			// Not on a place the build list keeps for a structure it has still to build.
			Bool reserved = FALSE;
			for (BuildListInfo *info = m_player->getBuildList(); info && !reserved; info = info->getNext())
				reserved = info->isBuildable() && TheGameLogic->findObjectByID(info->getObjectID()) == nullptr &&
					sqr(info->getLocation()->x - pos.x) + sqr(info->getLocation()->y - pos.y) < sqr(120.0f);
			if (legal && !reserved && groundReachable(base, pos, 0.0f))
			{
				*where = pos;
				return TRUE;
			}
		}
	}
	return FALSE;
}

void AIStrategy::updateIncomeBuildings()
{
	const AISkillSettings &sk = skill();
	if (sk.m_incomeRate <= 0.0f || sk.m_incomeBuildings <= 0 || TheControlBar == nullptr || m_bdActive)
		return;

	// A worker of ours, and the income structure its command set builds.
	Object *dozer = nullptr;
	const ThingTemplate *plan = nullptr;
	Int have = 0;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (obj == nullptr || obj->isEffectivelyDead())
					continue;
				if (isIncomeStructure(obj->getTemplate()))
					++have;
				else if (dozer == nullptr && obj->isKindOf(KINDOF_DOZER) && obj->getAI())
				{
					const CommandSet *set = TheControlBar->findCommandSet(obj->getCommandSetString());
					for (Int i = 0; set && i < MAX_COMMANDS_PER_SET && plan == nullptr; ++i)
					{
						const CommandButton *button = set->getCommandButton(i);
						if (button && button->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && isIncomeStructure(button->getThingTemplate()))
						{
							plan = button->getThingTemplate();
							dozer = obj;
						}
					}
				}
			}
		}
	}
	if (plan == nullptr)
		return;
	// As many as bring IncomeRate a minute (IncomeBuildings at most).
	const Real each = incomePerMinute(plan, sk);
	Int want = each > 0.0f ? (Int)ceilf(sk.m_incomeRate / each) : sk.m_incomeBuildings;
	if (want > sk.m_incomeBuildings)
		want = sk.m_incomeBuildings;
	if (have >= want)
		return;
	// One at a time; ones that were destroyed are replaced IncomeRebuilds times at most.
	Int lost = 0;
	BuildListInfo *waiting = waitingEntry(m_player, plan, &lost);
	if (waiting == nullptr && lost > sk.m_incomeRebuilds)
		return;

	const ThingTemplate *build = plan;
	const CanMakeType can = TheBuildAssistant->canMakeUnit(dozer, plan);
	if (can == CANMAKE_NO_PREREQ)
	{
		// Not unlocked yet: the structure that unlocks it first (one the worker can build).
		build = nullptr;
		for (Int p = 0; p < plan->getPrereqCount() && build == nullptr; ++p)
		{
			const ProductionPrerequisite *pre = plan->getNthPrereq(p);
			if (pre->isSatisfied(m_player))
				continue;
			const ThingTemplate *needed[4];
			const Int n = pre->getAllPossibleBuildFacilityTemplates(needed, 4);
			for (Int k = 0; k < n && build == nullptr; ++k)
			{
				const CanMakeType c = needed[k] ? TheBuildAssistant->canMakeUnit(dozer, needed[k]) : CANMAKE_NO_PREREQ;
				if (c == CANMAKE_OK || c == CANMAKE_NO_MONEY)
					build = needed[k];
			}
		}
		if (build == nullptr)
			return;
		// One of it that is already there (under construction, say) is waited for.
		for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
			for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
				for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done(); oit.advance())
					if (oit.cur() && !oit.cur()->isEffectivelyDead() && oit.cur()->getTemplate()->isEquivalentTo(build))
						return;
		waiting = waitingEntry(m_player, build, nullptr);
	}
	else if (can != CANMAKE_OK && can != CANMAKE_NO_MONEY)
		return;

	// One that the scripts planned and wait for: built now (the base builder takes priority entries first).
	if (waiting)
	{
		if (!waiting->isPriorityBuild())
		{
			waiting->markPriorityBuild();
			AI_TRACE("INCOME: %s on the build list is built now%s: %d of %d wanted (%.0f a minute each, %.0f wanted)", build->getName().str(),
				build == plan ? "" : " (it unlocks the income structure)", have, want, each, sk.m_incomeRate);
		}
		return;
	}

	Coord3D pos;
	if (!incomeSite(build, &pos))
		return;
	m_player->addToPriorityBuildList(build->getName(), &pos, build->getPlacementViewAngle());
	++m_incomeBuilt;
	AI_TRACE("INCOME: %s goes on the build list at (%.0f,%.0f)%s: %d of %d wanted (%.0f a minute each, %.0f wanted), %d lost so far",
		build->getName().str(), pos.x, pos.y, build == plan ? "" : " (it unlocks the income structure)", have, want, each, sk.m_incomeRate, lost);
}
