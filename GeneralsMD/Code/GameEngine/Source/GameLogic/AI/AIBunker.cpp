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

// AIBunker.cpp
// Standing garrisons for the Expert computer player.
//
// A defensive structure of ours that holds infantry (a garrison, or a bunker: a container on a structure whose passengers may fire
// out) is no use empty.  Read from the template, no names: the structure is ours and finished, its contain module is not a clinic, a
// tunnel or a zero slot container, it has places, its passengers may fire, and it is a defence (KindOf FS_BASE_DEFENSE, or armed, or
// not a production, power or other economic structure).  Such a post is
//   * filled and kept filled, the one on the side of the base where the attacks are expected first (the ways into the base of
//     AIGeo, else the side of the rally point),
//   * with a mix of anti-armour and anti-infantry troops: the mix of what the enemy model has seen (half and half without
//     sightings), picked by the damage of the unit against the biggest seen enemy vehicle and enemy infantry,
//   * from the infantry at home that belong to no wave (loose units, teams that are not on a wave), and when there are none, from new
//     infantry that the player trains for it (the unit that suits the missing class best, if the money allows; not more than two on
//     order),
//   * for good: the men inside are detached from the teams, so waves, raids, regroups and the base defence do not take them out; when
//     one dies, or the structure is rebuilt, the place is filled again.
// Whether a unit may enter is asked of the action manager.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

enum { BK_ENTERING = 0, BK_INSIDE = 1 };
enum { CLASS_ANTIINFANTRY = 0, CLASS_ANTIARMOUR = 1 };

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::bunkerOn() const
{
	return (skill().m_useBunkers || m_ai->isFeatureForced(AIPlayer::AIF_BUNKER)) && !m_ai->isFeatureOff(AIPlayer::AIF_BUNKER);
}

/// Is this structure of ours a post for a standing garrison (see the top of the file)?
Bool AIStrategy::isGarrisonPost( Object *obj ) const
{
	if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_NO_GARRISON))
		return FALSE;
	if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD) || obj->isDisabled())
		return FALSE;
	ContainModuleInterface *contain = obj->getContain();
	if (contain == nullptr || contain->isHealContain() || contain->isTunnelContain() || contain->isSpecialZeroSlotContainer() || contain->getContainMax() <= 0)
		return FALSE;
	if (!contain->isPassengerAllowedToFire())
		return FALSE;
	if (obj->isKindOf(KINDOF_FS_BASE_DEFENSE))
		return TRUE;
	const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
	if (f != nullptr && f->m_armed && f->m_canHitGround)
		return TRUE;
	// A container that is neither a defence nor anything else of use (no production, power, headquarters ...).
	if (obj->getProductionUpdateInterface() != nullptr || obj->isKindOf(KINDOF_COMMANDCENTER) || obj->isKindOf(KINDOF_FS_POWER) ||
			obj->isKindOf(KINDOF_FS_FACTORY) || obj->isKindOf(KINDOF_FS_SUPERWEAPON) || obj->isKindOf(KINDOF_FS_SUPPLY_CENTER) ||
			obj->isKindOf(KINDOF_FS_TECHNOLOGY) || obj->isKindOf(KINDOF_CASH_GENERATOR) || obj->isKindOf(KINDOF_REPAIR_PAD) ||
			obj->isKindOf(KINDOF_HEAL_PAD) || obj->isKindOf(KINDOF_SUPPLY_SOURCE))
		return FALSE;
	return TRUE;
}

Bool AIStrategy::isBunkerMan( ObjectID id ) const
{
	for (Int i = 0; i < m_numBunkerMen; ++i)
		if (m_bunkerMen[i].m_unit == id)
			return TRUE;
	return FALSE;
}

/// The enemy template (vehicle or infantry) that is the biggest in value among what was seen; false when none.
const AICombatFigures *AIStrategy::seenEnemyFigures( Bool infantry ) const
{
	const AICombatFigures *best = nullptr;
	Real bestValue = 0.0f;
	for (Int i = 0; i < m_enemy.numComposition(); ++i)
	{
		const AIComposition &c = m_enemy.composition()[i];
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || f->m_structure || f->m_airborne || f->m_infantry != infantry || !f->m_armed)
			continue;
		if (c.m_value > bestValue)
		{
			bestValue = c.m_value;
			best = f;
		}
	}
	return best;
}

/// Anti-armour or anti-infantry: the damage per second of the unit against the biggest enemy vehicle and enemy infantry that were
/// seen (a rough rule from the weapons when nothing was seen: the heavy hitters are anti-armour).
Int AIStrategy::bunkerClass( const AICombatFigures *f ) const
{
	if (f == nullptr)
		return CLASS_ANTIINFANTRY;
	const AICombatFigures *vehicle = seenEnemyFigures(FALSE);
	const AICombatFigures *infantry = seenEnemyFigures(TRUE);
	if (vehicle != nullptr && infantry != nullptr)
		return AICombatModel::damagePerSecond(f, vehicle) / (AICombatModel::damagePerSecond(f, infantry) + 0.01f) >= 0.6f ? CLASS_ANTIARMOUR : CLASS_ANTIINFANTRY;
	Real heaviest = 0.0f;
	for (Int i = 0; i < f->m_numWeapons; ++i)
		if (f->m_weapons[i].m_damage > heaviest)
			heaviest = f->m_weapons[i].m_damage;
	return heaviest >= 25.0f ? CLASS_ANTIARMOUR : CLASS_ANTIINFANTRY;
}

/// The share of the places that the anti-armour troops should have: the share of the enemy vehicles in what was seen.
Real AIStrategy::bunkerArmourShare() const
{
	const Real vehicles = m_enemy.roleValue(AIROLE_VEHICLE), infantry = m_enemy.roleValue(AIROLE_INFANTRY);
	if (vehicles + infantry < 200.0f)
		return 0.5f;
	Real share = vehicles / (vehicles + infantry);
	if (share < 0.25f) share = 0.25f;
	if (share > 0.75f) share = 0.75f;
	return share;
}

/// How much a post is on the side of the expected attacks: the ways into the base weighted by their shares, else near the rally point.
Real AIStrategy::postThreatScore( Object *post )
{
	const Coord3D *p = post->getPosition();
	if (m_geoReady && m_numEntrances > 0)
	{
		Real score = 0.0f;
		for (Int i = 0; i < m_numEntrances; ++i)
			score += m_entrances[i].m_weight / (1.0f + dist2D(*p, m_entrances[i].m_choke) / 200.0f);
		return score;
	}
	Coord3D rally;
	if (rallyPoint(&rally))
		return 1.0f / (1.0f + dist2D(*p, rally) / 200.0f);
	return 0.0f;
}

/// Keeps the posts filled.
void AIStrategy::updateBunkers()
{
	if (!bunkerOn())
	{
		m_numBunkerMen = 0;
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextBunker)
		return;
	m_nextBunker = now + 2 * LOGICFRAMES_PER_SECOND;

	// The men who are in (or on their way to) a post.
	for (Int i = 0; i < m_numBunkerMen; )
	{
		AIBunkerRecord &g = m_bunkerMen[i];
		Object *unit = TheGameLogic->findObjectByID(g.m_unit);
		Object *building = TheGameLogic->findObjectByID(g.m_building);
		Bool drop = FALSE;
		if (unit == nullptr || unit->isEffectivelyDead() || unit->getAI() == nullptr || building == nullptr || building->isEffectivelyDead())
		{
			drop = TRUE;
		}
		else
		{
			const Bool inside = unit->isContained() && unit->getContainedBy() == building;
			if (g.m_phase == BK_ENTERING)
			{
				if (inside)
				{
					g.m_phase = BK_INSIDE;
					++m_bunkerEntered;
				}
				else if (now - g.m_since > 25 * LOGICFRAMES_PER_SECOND)
				{
					unit->getAI()->aiIdle(CMD_FROM_AI);		// could not get in
					drop = TRUE;
				}
			}
			else if (!inside)
			{
				drop = TRUE;		// thrown out, or the structure was captured
			}
		}
		if (drop)
			m_bunkerMen[i] = m_bunkerMen[--m_numBunkerMen];
		else
			++i;
	}

	// The posts, the threatened side first.
	struct Post
	{
		Object *obj;
		Real score;
		Int free;
		Int armour;
		Int total;
	};
	Post posts[8];
	Int numPosts = 0;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (!isGarrisonPost(obj))
					continue;
				ContainModuleInterface *contain = obj->getContain();
				Int free = contain->getContainMax() - (Int)contain->getContainCount();
				Int armour = 0, total = 0;
				for (Int g = 0; g < m_numBunkerMen; ++g)
				{
					if (m_bunkerMen[g].m_building != obj->getID())
						continue;
					++total;
					armour += m_bunkerMen[g].m_class == CLASS_ANTIARMOUR ? 1 : 0;
					if (m_bunkerMen[g].m_phase == BK_ENTERING)
						--free;
				}
				const Real score = postThreatScore(obj);
				Int pos = numPosts;
				while (pos > 0 && posts[pos - 1].score < score)
					--pos;
				if (pos >= 8)
					continue;
				const Int last = numPosts < 8 ? numPosts : 7;
				for (Int k = last; k > pos; --k)
					posts[k] = posts[k - 1];
				posts[pos].obj = obj;
				posts[pos].score = score;
				posts[pos].free = free;
				posts[pos].armour = armour;
				posts[pos].total = total;
				if (numPosts < 8)
					++numPosts;
			}
		}
	}
	m_bunkerPosts = numPosts;
	Int openPlaces = 0;
	for (Int p = 0; p < numPosts; ++p)
		if (posts[p].free > 0)
			openPlaces += posts[p].free;
	if (openPlaces == 0)
	{
		m_bunkerQueued = 0;
		return;
	}

	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return;
	const Real armourShare = bunkerArmourShare();
	Int assigned = 0, missing = 0, wantedClass = CLASS_ANTIINFANTRY;
	// The men in the posts are a share of the army, not all of it: a field army that has nobody left cannot win and does not
	// need the bunkers either.
	Real postValue = 0.0f;
	for (Int g = 0; g < m_numBunkerMen; ++g)
	{
		Object *man = TheGameLogic->findObjectByID(m_bunkerMen[g].m_unit);
		const AICombatFigures *mf = man ? AICombatModel::figures(man->getTemplate()) : nullptr;
		postValue += mf ? mf->m_cost : 100.0f;
	}
	const Real shareLimit = skill().m_bunkerArmyShare;

	for (Int p = 0; p < numPosts && assigned < 3; ++p)
	{
		for (Int slot = 0; slot < posts[p].free && assigned < 3 && m_numBunkerMen < MAX_BUNKER_MEN; ++slot)
		{
			if (m_numBunkerMen >= 2 && postValue + 100.0f > shareLimit * (m_armyValue + postValue))
				break;			// enough of the army sits in the posts for now
			const Int want = (Real)posts[p].armour < armourShare * (Real)(posts[p].total + 1) - 0.01f ? CLASS_ANTIARMOUR : CLASS_ANTIINFANTRY;
			wantedClass = want;
			Object *pick = nullptr, *pickOther = nullptr;
			Real pickD = 0.0f, pickOtherD = 0.0f;
			Int pickClass = want;
			Int otherClass = want;

			// Candidates: infantry that belong to no wave: loose ones (not in any team of the strategy) and members of teams that
			// are not on a wave, near the base.
			for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
			{
				for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
				{
					Team *team = iter.cur();
					if (team == nullptr)
						continue;
					const AITeamRecord *rec = nullptr;
					for (Int r = 0; r < m_numTeams; ++r)
						if (m_teams[r].m_team == team->getID())
							rec = &m_teams[r];
					if (rec != nullptr && (rec->m_mode == AITEAM_ATTACKING || rec->m_mode == AITEAM_RETREATING || rec->m_mode == AITEAM_REGROUPING || rec->m_mode == AITEAM_DEFENDING))
						continue;
					const Bool loose = !isManageableTeam(team);
					if (!loose && rec == nullptr)
						continue;
					for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
					{
						Object *obj = oit.cur();
						if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr || isDetached(obj->getID()))
							continue;
						if (!obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_NO_GARRISON))
							continue;
						// Teams that are not managed (scripted teams under way) are not ours to take from; the player's own default team
						// (units that were ordered without a team: the ones trained for the posts) is.
						if (loose && team != m_player->getDefaultTeam())
							continue;
						const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
						if (f == nullptr || !f->m_armed || !f->m_canHitGround)
							continue;
						Real health = 1.0f;
						BodyModuleInterface *body = obj->getBodyModule();
						if (body != nullptr && body->getMaxHealth() > 0.0f)
							health = body->getHealth() / body->getMaxHealth();
						if (health < 0.5f || dist2D(*obj->getPosition(), base) > m_ai->m_baseRadius + 500.0f)
							continue;
						if (!TheActionManager->canEnterObject(obj, posts[p].obj, CMD_FROM_AI, CHECK_CAPACITY))
							continue;
						const Int cls = bunkerClass(f);
						const Real d = dist2D(*obj->getPosition(), *posts[p].obj->getPosition());
						if (cls == want)
						{
							if (pick == nullptr || d < pickD) { pick = obj; pickD = d; pickClass = cls; }
						}
						else if (pickOther == nullptr || d < pickOtherD)
						{
							pickOther = obj;
							pickOtherD = d;
							otherClass = cls;
						}
					}
				}
			}
			if (pick == nullptr)
			{
				pick = pickOther;
				pickClass = otherClass;
			}
			if (pick == nullptr)
			{
				++missing;
				continue;
			}
			AIBunkerRecord &g = m_bunkerMen[m_numBunkerMen++];
			g.m_unit = pick->getID();
			g.m_building = posts[p].obj->getID();
			g.m_since = now;
			g.m_phase = BK_ENTERING;
			g.m_class = pickClass;
			{
				const AICombatFigures *pf = AICombatModel::figures(pick->getTemplate());
				postValue += pf ? pf->m_cost : 100.0f;
			}
			posts[p].total++;
			posts[p].armour += pickClass == CLASS_ANTIARMOUR ? 1 : 0;
			if (m_bunkerQueued > 0)
				--m_bunkerQueued;
			// The man leaves his team (the scripts then see a team that is short of a man and replace him; a man who sat in a
			// bunker for good as a member of the team would stop that).
			if (pick->getTeam() != m_player->getDefaultTeam())
				pick->setTeam(m_player->getDefaultTeam());
			pick->getAI()->aiEnter(posts[p].obj, CMD_FROM_AI);
			++assigned;
			AI_TRACE("BUNKER: %s %u (%s) goes into %s %u (%d of %d places taken; threat side score %.2f)", pick->getTemplate()->getName().str(), pick->getID(),
				pickClass == CLASS_ANTIARMOUR ? "anti-armour" : "anti-infantry", posts[p].obj->getTemplate()->getName().str(), posts[p].obj->getID(),
				posts[p].total, posts[p].obj->getContain()->getContainMax(), posts[p].score);
		}
	}

	// Nobody to send: train the missing men (a few at a time, when the money allows).
	if (missing > 0 && now >= m_nextBunkerTrain)
	{
		m_nextBunkerTrain = now + 5 * LOGICFRAMES_PER_SECOND;
		if (m_bunkerQueued > 0 && now - m_bunkerQueueFrame > 60 * LOGICFRAMES_PER_SECOND)
			m_bunkerQueued = 0;		// the ones ordered earlier never showed up (or were taken for something else)
		if (m_bunkerQueued < 2 && (m_numBunkerMen < 2 || postValue + 100.0f <= shareLimit * (m_armyValue + postValue)))
			trainForBunker(wantedClass);
	}
}

/// Queues an infantry unit of the wanted class at a factory that can make it.
void AIStrategy::trainForBunker( Int wantedClass )
{
	if (TheControlBar == nullptr)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real reserve = skill().m_bunkerReserve;
	Object *bestFactory = nullptr;
	const ThingTemplate *bestTemplate = nullptr;
	Real bestScore = 0.0f;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *factory = oit.cur();
				if (factory == nullptr || factory->isEffectivelyDead() || !factory->isKindOf(KINDOF_STRUCTURE) || factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					continue;
				ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
				if (pu == nullptr || pu->getProductionCount() > 1)
					continue;
				const CommandSet *set = TheControlBar->findCommandSet(factory->getCommandSetString());
				if (set == nullptr)
					continue;
				for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
				{
					const CommandButton *button = set->getCommandButton(i);
					if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD)
						continue;
					const ThingTemplate *tmpl = button->getThingTemplate();
					if (tmpl == nullptr || !tmpl->isKindOf(KINDOF_INFANTRY) || tmpl->isKindOf(KINDOF_NO_GARRISON))
						continue;
					const AICombatFigures *f = AICombatModel::figures(tmpl);
					if (f == nullptr || !f->m_armed || !f->m_canHitGround || f->m_cost <= 0.0f)
						continue;
					if (TheBuildAssistant->canMakeUnit(factory, tmpl) != CANMAKE_OK)
						continue;
					if ((Real)m_player->getMoney()->countMoney() < f->m_cost + reserve)
						continue;
					// The unit has to be able to enter (the transport slots of the posts are asked at the moment of the order).
					Real score = (bunkerClass(f) == wantedClass ? 100.0f : 0.0f) + 100.0f / (1.0f + f->m_cost / 100.0f);
					if (bestTemplate == nullptr || score > bestScore)
					{
						bestScore = score;
						bestTemplate = tmpl;
						bestFactory = factory;
					}
				}
			}
		}
	}
	if (bestFactory == nullptr)
		return;
	ProductionUpdateInterface *pu = bestFactory->getProductionUpdateInterface();
	if (pu->queueCreateUnit(bestTemplate, pu->requestUniqueUnitID()))
	{
		++m_bunkerQueued;
		m_bunkerQueueFrame = now;
		++m_bunkerTrained;
		AI_TRACE("BUNKER: no infantry free for the empty places: %s ordered at %s %u (%s wanted)", bestTemplate->getName().str(), bestFactory->getTemplate()->getName().str(),
			bestFactory->getID(), wantedClass == CLASS_ANTIARMOUR ? "anti-armour" : "anti-infantry");
	}
}
