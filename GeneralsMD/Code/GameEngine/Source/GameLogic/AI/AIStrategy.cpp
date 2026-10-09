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

// AIStrategy.cpp
// The strategic layer of the Expert skirmish computer player.
//
// What it adds on top of the scripted skirmish AI (the team build scripts and the base build list stay
// in charge of *what exists*; this layer decides *how it is used*):
//
//  * an enemy model fed only from what the player can see (AIEnemyModel),
//  * team selection weighted by how well a team's weapons work against the observed enemy army,
//  * attack waves: finished teams gather at home and go together,
//  * a target for the wave chosen by value against defence, instead of the nearest thing,
//  * fights are weighed: a team that is losing pulls back, regroups and returns,
//  * idle teams get new orders, threats at the base pull teams home,
//  * the economy: idle factories are put to work, more gatherers late in the game, expansion,
//  * superweapons are aimed at the densest cluster of known value.
//
// Every decision uses synchronised state and GameLogicRandomValue only, floating point math that is
// exactly rounded (no libm), and arrays whose order follows the (deterministic) object list.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AIMatch.h"
#include "Common/BuildAssistant.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"


//-------------------------------------------------------------------------------------------------
// small helpers
//-------------------------------------------------------------------------------------------------
static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static inline Real clampReal(Real v, Real lo, Real hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static const ThingTemplate *templateOf(UnsignedShort id)
{
	return TheThingFactory->findByTemplateID(id);
}

/// Does the thing take part in a fight?  (armed, and not a building)
static inline Bool isCombatUnit(const Object *obj)
{
	if (obj->isKindOf(KINDOF_STRUCTURE) || obj->isEffectivelyDead())
		return FALSE;
	const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
	return f && f->m_armed;
}

//-------------------------------------------------------------------------------------------------
AIStrategy::AIStrategy( AIPlayer *ai, Player *p ) :
	m_ai(ai),
	m_player(p),
	m_nextScan(0),
	m_nextTeamEval(0),
	m_nextEconomy(0),
	m_nextPowers(0),
	m_teamCursor(0),
	m_numTeams(0),
	m_armyState(ARMY_GATHER),
	m_armyStateFrame(0),
	m_armyValue(0.0f),
	m_armyPeak(0.0f),
	m_armyGrowthFrame(0),
	m_launchValue(0.0f),
	m_rallySet(FALSE),
	m_nextRally(0),
	m_productionStarved(FALSE),
	m_lastExpandFrame(0),
	m_expansions(0),
	m_threatSince(0),
	m_lastThreatResponse(0),
	m_powerReadySince(0),
	m_siegeShortage(0.5f),
	m_scoutID(INVALID_ID),
	m_scoutUntil(0),
	m_nextScout(0),
	m_savingSince(0),
	m_noSavingUntil(0),
	m_numSteps(0),
	m_tacticTeam(0),
	m_tacticUnit(0),
	m_kiteStarts(0),
	m_kiteResumes(0),
	m_kiteRejectFast(0),
	m_kiteRejectCorner(0),
	m_mergedTeams(0),
	m_mergedUnits(0),
	m_launchBlockedSince(0),
	m_nextLaunchCheck(0),
	m_waveBadSince(0),
	m_nextWaveCheck(0),
	m_launchesHeld(0),
	m_launchesForced(0),
	m_pullbacks(0),
	m_splitPicks(0),
	m_splitSwitches(0),
	m_threatSwitches(0),
	m_supportPicks(0),
	m_longRangePicks(0),
	m_trace(FALSE),
	m_nextStatus(0)
{
	memset(m_teams, 0, sizeof(m_teams));
	memset(m_ledger, 0, sizeof(m_ledger));
	memset(m_steps, 0, sizeof(m_steps));
	m_scoutTarget.zero();
	m_waveObjective.zero();
	m_rally.zero();
	m_threatPos.zero();
}

AIStrategy::~AIStrategy()
{
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::newMap()
{
	AICombatModel::reset();
	m_enemy.reset();
	// Test bench variant: "trace" prints decisions; "off-focus+wave+..." disables single features for A/B runs.
	const AsciiString variant = AIMatch::getPlayerVariant(m_player);
	m_trace = strstr(variant.str(), "trace") != nullptr;
	UnsignedInt off = 0;
	static const struct { const char *name; Int bit; } names[] = {
		{ "focus", AIPlayer::AIF_FOCUS }, { "wave", AIPlayer::AIF_WAVE }, { "retreat", AIPlayer::AIF_RETREAT },
		{ "scout", AIPlayer::AIF_SCOUT }, { "counter", AIPlayer::AIF_COUNTER }, { "save", AIPlayer::AIF_SAVE },
		{ "starve", AIPlayer::AIF_STARVE }, { "siege", AIPlayer::AIF_SIEGE }, { "defend", AIPlayer::AIF_DEFEND },
		{ "split", AIPlayer::AIF_SPLIT }, { "threat", AIPlayer::AIF_THREAT }, { "kite", AIPlayer::AIF_KITE }, { "fight", AIPlayer::AIF_FIGHT }, { "merge", AIPlayer::AIF_MERGE } };
	const char *offList = strstr(variant.str(), "off-");
	if (offList)
	{
		for (size_t i = 0; i < ARRAY_SIZE(names); ++i)
			if (strstr(offList, names[i].name))
				off |= names[i].bit;
	}
	m_ai->setFeaturesOff(off);
}

// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

//-------------------------------------------------------------------------------------------------
const AISkillSettings &AIStrategy::skill() const
{
	return TheAI->getAiData()->m_expertSkill;
}

UnsignedInt AIStrategy::secondsToFrames( Real seconds ) const
{
	Int f = (Int)(seconds * LOGICFRAMES_PER_SECOND + 0.5f);
	return f < 1 ? 1 : (UnsignedInt)f;
}

/// A deliberate slip: with the chance of the skill settings the AI takes the second best choice.
Bool AIStrategy::rollMistake() const
{
	const Real chance = skill().m_mistakeChance;
	if (chance <= 0.0f)
		return FALSE;
	return GameLogicRandomValue(0, 9999) < (Int)(chance * 10000.0f);
}

//-------------------------------------------------------------------------------------------------
Player *AIStrategy::enemyPlayer() const
{
	return m_player->getCurrentEnemy();
}

Bool AIStrategy::enemyStartPosition( Coord3D *pos ) const
{
	Player *enemy = enemyPlayer();
	if (enemy == nullptr)
		return FALSE;
	// The start positions of a map are public knowledge in a skirmish.
	AsciiString name;
	name.format("Player_%d_Start", enemy->getMpStartIndex() + 1);
	Waypoint *way = TheTerrainLogic->getWaypointByName(name);
	if (way == nullptr)
		return FALSE;
	*pos = *way->getLocation();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// per frame
//-------------------------------------------------------------------------------------------------
void AIStrategy::update()
{
	const UnsignedInt now = TheGameLogic->getFrame();

	// Look at the enemy.  The sweep over the object list is sliced; when it has completed once the
	// next sweep waits for the scouting interval of the skill settings.
	if (now >= m_nextScan)
	{
		if (m_enemy.scan(m_player, 150))
		{
			m_enemy.refresh(m_player, now);
			m_nextScan = now + secondsToFrames(skill().m_scoutSeconds);
			if (m_trace && now >= m_nextStatus)
			{
				m_nextStatus = now + 20 * LOGICFRAMES_PER_SECOND;
				Int unmanaged = 0, managedUnits = 0;
				for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
					for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
					{
						Team *tm = iter.cur();
						Int n = 0;
						for (DLINK_ITERATOR<Object> oit = tm->iterate_TeamMemberList(); !oit.done(); oit.advance())
							if (oit.cur() && isCombatUnit(oit.cur())) ++n;
						if (isManageableTeam(tm)) managedUnits += n; else if (n > 0) { unmanaged += n; AI_TRACE("  unmanaged team %s active %d units %d", tm->getPrototype()->getName().str(), tm->isActive() ? 1 : 0, n); }
					}
				AI_TRACE("army: managed units %d unmanaged units %d value %.0f siege shortage %.2f", managedUnits, unmanaged, m_armyValue, m_siegeShortage);
				AI_TRACE("target picks %d: changed by split fire %d, by the threat rules %d; support units %d; out-ranging units %d",
					m_splitPicks, m_splitSwitches, m_threatSwitches, m_supportPicks, m_longRangePicks);
				AI_TRACE("kiting: %d steps back, %d resumed; refused: %d enemy faster, %d no room", m_kiteStarts, m_kiteResumes, m_kiteRejectFast, m_kiteRejectCorner);
				AI_TRACE("merge: %d new teams kept for the next wave, %d reinforcements sent to the rally point", m_mergedTeams, m_mergedUnits);
				AI_TRACE("fight check: launches held %d (forced anyway %d), waves pulled back %d", m_launchesHeld, m_launchesForced, m_pullbacks);
				AI_TRACE("status: contacts %d  inf %.0f veh %.0f air %.0f def %.0f prod %.0f eco %.0f other %.0f  teams %d  money %u",
					m_enemy.numContacts(), m_enemy.roleValue(AIROLE_INFANTRY), m_enemy.roleValue(AIROLE_VEHICLE), m_enemy.roleValue(AIROLE_AIRCRAFT),
					m_enemy.roleValue(AIROLE_DEFENCE), m_enemy.roleValue(AIROLE_PRODUCTION), m_enemy.roleValue(AIROLE_ECONOMY),
					m_enemy.roleValue(AIROLE_STRUCTURE), m_numTeams, m_player->getMoney()->countMoney());
			}
		}
	}

	if (now >= m_nextTeamEval)
	{
		m_nextTeamEval = now + secondsToFrames(skill().m_attentionSeconds);
		updateTeams();
	}

	if (now >= m_nextEconomy)
	{
		m_nextEconomy = now + 2 * LOGICFRAMES_PER_SECOND;
		updateEconomy();
		updateSiege();
	}

	updateTactics();

	if (now >= m_nextPowers && skill().m_smartPowers)
	{
		m_nextPowers = now + 3 * LOGICFRAMES_PER_SECOND;
		updatePowers();
	}
}

//-------------------------------------------------------------------------------------------------
// value of things
//-------------------------------------------------------------------------------------------------
Real AIStrategy::teamValue( Team *team )
{
	Real v = 0.0f;
	for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
	{
		Object *obj = it.cur();
		if (obj == nullptr || !isCombatUnit(obj))
			continue;
		const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
		BodyModuleInterface *body = obj->getBodyModule();
		Real health = 1.0f;
		if (body && body->getMaxHealth() > 0.0f)
			health = clampReal(body->getHealth() / body->getMaxHealth(), 0.1f, 1.0f);
		v += f->m_cost * (0.3f + 0.7f * health);
	}
	return v;
}

//-------------------------------------------------------------------------------------------------
// counter building
//-------------------------------------------------------------------------------------------------
Real AIStrategy::teamCounterScore( const TeamPrototype *proto ) const
{
	if (skill().m_counterStrength <= 0.0f || m_enemy.numComposition() == 0 || m_ai->isFeatureOff(AIPlayer::AIF_COUNTER))
		return 0.0f;

	// Weight of the enemy kinds: their value; buildings count less (the army comes first) and together
	// never more than two thirds of the army, but they must come down in the end, so they do count.
	Real armyWeight = 0.0f, structureWeight = 0.0f;
	for (Int e = 0; e < m_enemy.numComposition(); ++e)
	{
		const AIComposition &c = m_enemy.composition()[e];
		const AICombatFigures *f = AICombatModel::figures(templateOf(c.m_templateID));
		if (f == nullptr)
			continue;
		if (f->m_structure)
			structureWeight += c.m_value;
		else
			armyWeight += c.m_value;
	}
	// An army that could not bring a base down in good time cares more about what is good against buildings.
	Real structureScale = 0.35f + 1.5f * m_siegeShortage;
	const Real structureShare = 0.67f + 1.5f * m_siegeShortage;
	if (armyWeight > 0.0f && structureWeight * structureScale > armyWeight * structureShare)
		structureScale = armyWeight * structureShare / structureWeight;

	Real totalWeight = 0.0f;
	Real weight[AIEnemyModel::MAX_COMPOSITION];
	const AICombatFigures *enemyFig[AIEnemyModel::MAX_COMPOSITION];
	for (Int e = 0; e < m_enemy.numComposition(); ++e)
	{
		const AIComposition &c = m_enemy.composition()[e];
		enemyFig[e] = AICombatModel::figures(templateOf(c.m_templateID));
		weight[e] = 0.0f;
		if (enemyFig[e] == nullptr)
			continue;
		weight[e] = c.m_value * (enemyFig[e]->m_structure ? structureScale : 1.0f);
		totalWeight += weight[e];
	}
	if (totalWeight <= 0.0f)
		return 0.0f;

	const TeamTemplateInfo *info = proto->getTemplateInfo();
	Real score = 0.0f;
	for (Int e = 0; e < m_enemy.numComposition(); ++e)
	{
		if (weight[e] <= 0.0f)
			continue;
		Real sum = 0.0f;
		Real count = 0.0f;
		for (Int u = 0; u < info->m_numUnitsInfo; ++u)
		{
			const ThingTemplate *tt = TheThingFactory->findTemplate(info->m_unitsInfo[u].unitThingName);
			const AICombatFigures *f = AICombatModel::figures(tt);
			if (f == nullptr || !f->m_armed)
				continue;	// workers and the like neither help nor hurt a team's score
			const Real n = (info->m_unitsInfo[u].minUnits + info->m_unitsInfo[u].maxUnits) * 0.5f + 0.001f;
			sum += n * AICombatModel::matchup(f, enemyFig[e]);
			count += n;
		}
		if (count > 0.0f)
			score += (weight[e] / totalWeight) * (sum / count);
	}
	AI_TRACE("counter score of %s: %.2f", proto->getName().str(), score);
	return score;
}

Real AIStrategy::teamWeight( const TeamPrototype *proto, Int hiPri ) const
{
	const Int steps = hiPri - proto->getTemplateInfo()->m_productionPriority;
	Real w;
	if (steps >= 0)
		w = 1.0f / (Real)(1 << (steps > 6 ? 6 : steps));
	else
		w = (Real)(1 << (-steps > 6 ? 6 : -steps));
	Real counter = 1.0f + skill().m_counterStrength * 2.2f * teamCounterScore(proto);
	if (counter < 0.1f)
		counter = 0.1f;
	return w * counter;
}

Bool AIStrategy::shouldSaveFor( const TeamPrototype *proto, Real cost )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_noSavingUntil || m_ai->isFeatureOff(AIPlayer::AIF_SAVE))
		return FALSE;
	if (m_player->getAttackedFrame() + 8 * LOGICFRAMES_PER_SECOND > now)
		return FALSE;	// under fire: the units are needed now

	// Reachable within a minute?  Income per minute is what the economy actually delivered during the last minute.
	const Real money = (Real)m_player->getMoney()->countMoney();
	const Real income = (Real)m_player->getMoney()->getCashPerMinute();
	if (money + income < cost)
		return FALSE;

	if (m_savingSince == 0)
	{
		m_savingSince = now;
		AI_TRACE("saving for team %s (cost %.0f, money %.0f)", proto->getName().str(), cost, money);
	}
	if (now - m_savingSince > 75 * LOGICFRAMES_PER_SECOND)
	{
		m_savingSince = 0;
		m_noSavingUntil = now + 40 * LOGICFRAMES_PER_SECOND;	// enough waiting; spend something
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// objectives
//-------------------------------------------------------------------------------------------------
Bool AIStrategy::chooseObjective( const Coord3D *from, Real ourPower, Coord3D *objective )
{
	struct Cluster
	{
		Int cx, cy;
		Real prize, danger, sumX, sumY, sumW;
	};
	enum { MAX_CLUSTERS = 40 };
	Cluster clusters[MAX_CLUSTERS];
	Int numClusters = 0;
	const Real CELL = 400.0f;

	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		const AICombatFigures *f = AICombatModel::figures(templateOf(c.m_templateID));
		if (f == nullptr)
			continue;

		const Real health = 0.25f + 0.75f * (c.m_healthPct / 100.0f);
		const Real value = f->m_cost * health;

		// What taking the thing out is worth to us.
		Real prizeMul;
		switch (c.m_role)
		{
			case AIROLE_PRODUCTION: prizeMul = 1.4f; break;
			case AIROLE_ECONOMY: prizeMul = 1.3f; break;
			case AIROLE_STRUCTURE: prizeMul = 0.8f; break;
			case AIROLE_DEFENCE: prizeMul = 0.4f; break;
			case AIROLE_AIRDEFENCE: prizeMul = 0.3f; break;
			default: prizeMul = 0.5f; break;	// armies move; chasing them is the fight's job
		}

		Int cx = (Int)floorf(c.m_pos.x / CELL);
		Int cy = (Int)floorf(c.m_pos.y / CELL);
		Int k = 0;
		while (k < numClusters && !(clusters[k].cx == cx && clusters[k].cy == cy))
			++k;
		if (k == numClusters)
		{
			if (numClusters >= MAX_CLUSTERS)
				continue;
			clusters[k].cx = cx;
			clusters[k].cy = cy;
			clusters[k].prize = clusters[k].danger = clusters[k].sumX = clusters[k].sumY = clusters[k].sumW = 0.0f;
			++numClusters;
		}
		Cluster &cl = clusters[k];
		cl.prize += value * prizeMul;
		if (f->m_armed)
			cl.danger += value;
		cl.sumX += c.m_pos.x * value;
		cl.sumY += c.m_pos.y * value;
		cl.sumW += value;
	}

	// The enemy base is a target before anything of it has been seen, and stays one until we have looked at
	// the place and found it empty of structures.
	Coord3D start;
	if (enemyStartPosition(&start))
	{
		Bool known = FALSE;
		for (Int k = 0; k < numClusters && !known; ++k)
			known = clusters[k].cx == (Int)floorf(start.x / CELL) && clusters[k].cy == (Int)floorf(start.y / CELL);
		for (Int i = 0; i < m_enemy.numContacts() && !known; ++i)
			known = m_enemy.contacts()[i].m_role >= AIROLE_DEFENCE && dist2D(m_enemy.contacts()[i].m_pos, start) < 500.0f;
		const Bool looked = ThePartitionManager->getShroudStatusForPlayer(m_player->getPlayerIndex(), &start) == CELLSHROUD_CLEAR;
		if (!known && !looked && numClusters < MAX_CLUSTERS)
		{
			Cluster &cl = clusters[numClusters++];
			cl.cx = (Int)floorf(start.x / CELL);
			cl.cy = (Int)floorf(start.y / CELL);
			cl.prize = 2500.0f;
			cl.danger = 0.5f * m_enemy.armyValue();
			cl.sumX = start.x * 100.0f;
			cl.sumY = start.y * 100.0f;
			cl.sumW = 100.0f;
		}
	}

	// Score every cluster.  Valuable, near, and not much better defended than we are strong.
	Int best = -1, second = -1;
	Real bestScore = 0.0f, secondScore = 0.0f;
	for (Int k = 0; k < numClusters; ++k)
	{
		const Cluster &cl = clusters[k];
		if (cl.prize <= 0.0f || cl.sumW <= 0.0f)
			continue;
		Coord3D center;
		center.x = cl.sumX / cl.sumW;
		center.y = cl.sumY / cl.sumW;
		center.z = 0.0f;
		const Real dist = dist2D(*from, center);
		const Real score = cl.prize / (1.0f + dist / 1500.0f) * (ourPower + 50.0f) / (ourPower + 0.9f * cl.danger + 50.0f);
		if (score > bestScore)
		{
			second = best; secondScore = bestScore;
			best = k; bestScore = score;
		}
		else if (score > secondScore)
		{
			second = k; secondScore = score;
		}
	}

	if (best >= 0)
	{
		Int pick = (second >= 0 && rollMistake()) ? second : best;
		objective->x = clusters[pick].sumX / clusters[pick].sumW;
		objective->y = clusters[pick].sumY / clusters[pick].sumW;
		objective->z = TheTerrainLogic->getGroundHeight(objective->x, objective->y);
		return TRUE;
	}

	// Nothing worth attacking is known: search the unexplored map.
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	for (Int tries = 0; tries < 10; ++tries)
	{
		Coord3D p;
		p.x = extent.lo.x + (extent.hi.x - extent.lo.x) * (GameLogicRandomValue(5, 95) / 100.0f);
		p.y = extent.lo.y + (extent.hi.y - extent.lo.y) * (GameLogicRandomValue(5, 95) / 100.0f);
		p.z = 0.0f;
		if (ThePartitionManager->getShroudStatusForPlayer(m_player->getPlayerIndex(), &p) != CELLSHROUD_CLEAR)
		{
			p.z = TheTerrainLogic->getGroundHeight(p.x, p.y);
			*objective = p;
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/// Armed value of living units (ours or visible enemy ones) around a position.
Real AIStrategy::armyValueNear( const Coord3D *pos, Real radius, Bool enemies ) const
{
	Real v = 0.0f;
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(pos, radius, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	for (Object *obj = iter->first(); obj; obj = iter->next())
	{
		const Relationship r = m_player->getRelationship(obj->getTeam());
		if (enemies ? (r != ENEMIES) : (r == ENEMIES || obj->getControllingPlayer() != m_player))
			continue;
		if (!isCombatUnit(obj))
			continue;
		if (enemies && obj->getShroudedStatus(m_player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
			continue;
		v += AICombatModel::figures(obj->getTemplate())->m_cost;
	}
	return v;
}

//-------------------------------------------------------------------------------------------------
// fights
//-------------------------------------------------------------------------------------------------
namespace
{
	struct Group
	{
		const AICombatFigures *f;
		Int n;
		Real healthSum;
	};
	enum { MAX_GROUPS = 16 };

	void addToGroups(Group *groups, Int &numGroups, const AICombatFigures *f, Real health)
	{
		for (Int i = 0; i < numGroups; ++i)
		{
			if (groups[i].f == f)
			{
				groups[i].n += 1;
				groups[i].healthSum += health;
				return;
			}
		}
		if (numGroups < MAX_GROUPS)
		{
			groups[numGroups].f = f;
			groups[numGroups].n = 1;
			groups[numGroups].healthSum = health;
			++numGroups;
		}
	}

	/// Value destroyed per second by group a against group b, fire spread over the targets it can hit.
	Real destructionRate(const Group *a, Int na, const Group *b, Int nb)
	{
		Real total = 0.0f;
		for (Int i = 0; i < na; ++i)
		{
			Real weightSum = 0.0f;
			Real rate = 0.0f;
			for (Int j = 0; j < nb; ++j)
			{
				const Real dps = AICombatModel::damagePerSecond(a[i].f, b[j].f);
				if (dps <= 0.0f)
					continue;
				weightSum += b[j].n;
				rate += b[j].n * dps * b[j].f->m_cost / b[j].f->m_maxHealth;
			}
			if (weightSum > 0.0f)
				total += a[i].n * rate / weightSum;
		}
		return total;
	}
}

/**
 * Who wins the fight around a position, by Lanchester's reasoning: each side's rate of destroying
 * the other's value, against the value it has to lose.  Everything counts that is within reach:
 * our armed units (and allies'), the enemy's visible armed units, and enemy defences that cover the place.
 * Returns ours/theirs, 99 when there is nothing hostile, clamped to [0.05, 20].
 */
Real AIStrategy::fightAdvantage( const Coord3D *center, Real radius, Real *ourPower, Real *theirPower ) const
{
	Group ours[MAX_GROUPS], theirs[MAX_GROUPS];
	Int numOurs = 0, numTheirs = 0;
	Real valueOurs = 0.0f, valueTheirs = 0.0f;

	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(center, radius + 250.0f, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	for (Object *obj = iter->first(); obj; obj = iter->next())
	{
		if (obj->isOffMap() || obj->isContained())
			continue;
		const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
		if (f == nullptr || !f->m_armed)
			continue;
		if (obj->isKindOf(KINDOF_UNATTACKABLE) || obj->isKindOf(KINDOF_PROJECTILE) || obj->isKindOf(KINDOF_MINE))
			continue;
		if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
			continue;

		const Real d = dist2D(*center, *obj->getPosition());
		BodyModuleInterface *body = obj->getBodyModule();
		Real health = 1.0f;
		if (body && body->getMaxHealth() > 0.0f)
			health = clampReal(body->getHealth() / body->getMaxHealth(), 0.05f, 1.0f);

		const Relationship r = m_player->getRelationship(obj->getTeam());
		if (r == ENEMIES)
		{
			if (obj->getShroudedStatus(m_player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
				continue;
			if (obj->testStatus(OBJECT_STATUS_STEALTHED) && !obj->testStatus(OBJECT_STATUS_DETECTED))
				continue;
			// Defences only matter when the fight is within their reach; units within the sight of the fight.
			const Real reach = f->m_structure ? f->m_range + 120.0f : radius;
			if (d > reach)
				continue;
			addToGroups(theirs, numTheirs, f, health);
			valueTheirs += f->m_cost * health;
		}
		else if (r == ALLIES || obj->getControllingPlayer() == m_player)
		{
			if (f->m_structure)
			{
				// Our own defences help if the fight is in their reach.
				if (d > f->m_range + 60.0f)
					continue;
			}
			else if (d > radius)
			{
				continue;
			}
			addToGroups(ours, numOurs, f, health);
			valueOurs += f->m_cost * health;
		}
	}

	if (ourPower)
		*ourPower = valueOurs;
	if (theirPower)
		*theirPower = valueTheirs;

	if (valueTheirs <= 0.0f)
		return 99.0f;
	if (valueOurs <= 0.0f)
		return 0.05f;

	const Real rateOurs = destructionRate(ours, numOurs, theirs, numTheirs);
	const Real rateTheirs = destructionRate(theirs, numTheirs, ours, numOurs);
	if (rateTheirs <= 0.0f)
		return 20.0f;
	if (rateOurs <= 0.0f)
		return 0.05f;
	return clampReal((valueOurs * rateOurs) / (valueTheirs * rateTheirs), 0.05f, 20.0f);
}

/**
 * The same weighing for a fight that has not started: all of our field teams against what has been seen
 * of the enemy around a place (units that are within 'radius', defences that cover it).  Nothing is read
 * from objects we cannot see now: it is the enemy model's memory.
 */
Real AIStrategy::forecastAdvantage( const Coord3D *where, Real radius, Real *ourPower, Real *theirPower ) const
{
	Group ours[MAX_GROUPS], theirs[MAX_GROUPS];
	Int numOurs = 0, numTheirs = 0;
	Real valueOurs = 0.0f, valueTheirs = 0.0f;

	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || !isCombatUnit(obj))
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			BodyModuleInterface *body = obj->getBodyModule();
			Real health = 1.0f;
			if (body && body->getMaxHealth() > 0.0f)
				health = clampReal(body->getHealth() / body->getMaxHealth(), 0.05f, 1.0f);
			addToGroups(ours, numOurs, f, health);
			valueOurs += f->m_cost * health;
		}
	}

	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		const AICombatFigures *f = AICombatModel::figures(templateOf(c.m_templateID));
		if (f == nullptr || !f->m_armed)
			continue;
		const Real d = dist2D(c.m_pos, *where);
		const Real reach = f->m_structure ? f->m_range + 120.0f : radius;
		if (d > reach)
			continue;
		const Real health = 0.25f + 0.75f * (c.m_healthPct / 100.0f);
		addToGroups(theirs, numTheirs, f, health);
		valueTheirs += f->m_cost * health;
	}

	if (ourPower)
		*ourPower = valueOurs;
	if (theirPower)
		*theirPower = valueTheirs;
	if (valueTheirs <= 0.0f)
		return 99.0f;
	if (valueOurs <= 0.0f)
		return 0.05f;
	const Real rateOurs = destructionRate(ours, numOurs, theirs, numTheirs);
	const Real rateTheirs = destructionRate(theirs, numTheirs, ours, numOurs);
	if (rateTheirs <= 0.0f)
		return 20.0f;
	if (rateOurs <= 0.0f)
		return 0.05f;
	return clampReal((valueOurs * rateOurs) / (valueTheirs * rateTheirs), 0.05f, 20.0f);
}

/**
 * Before a wave goes: is the fight at its objective one it can win?  If it is clearly losing, it waits at the
 * rally point for reinforcements (the army keeps growing), but not for ever: after LaunchBlockSeconds it goes anyway.
 * Returns true when the wave may go.
 */
Bool AIStrategy::checkWaveLaunch( const Coord3D *objective )
{
	const AISkillSettings &sk = skill();
	if (!sk.m_useFightCheck || m_ai->isFeatureOff(AIPlayer::AIF_FIGHT))
		return TRUE;
	const UnsignedInt now = TheGameLogic->getFrame();
	Real ourPower = 0.0f, theirPower = 0.0f;
	const Real advantage = forecastAdvantage(objective, 450.0f, &ourPower, &theirPower);
	if (advantage >= sk.m_launchAdvantage)
	{
		m_launchBlockedSince = 0;
		return TRUE;
	}
	if (m_launchBlockedSince == 0)
	{
		m_launchBlockedSince = now;
		++m_launchesHeld;
		AI_TRACE("wave HELD at the rally point: advantage %.2f (ours %.0f, theirs %.0f at (%.0f,%.0f))", advantage, ourPower, theirPower, objective->x, objective->y);
	}
	if (now - m_launchBlockedSince >= secondsToFrames(sk.m_launchBlockSeconds))
	{
		AI_TRACE("wave goes anyway after waiting: advantage %.2f", advantage);
		++m_launchesForced;
		m_launchBlockedSince = 0;
		return TRUE;
	}
	m_nextLaunchCheck = now + 3 * LOGICFRAMES_PER_SECOND;
	return FALSE;
}

/**
 * A wave that is on its way to an objective it has not reached: compare it with what is known there once more
 * (the enemy model has been updated since the launch).  If it stays clearly the weaker side for a few seconds, the
 * wave turns back, regroups at the rally point and goes again when it is stronger (checkWaveLaunch decides that).
 */
void AIStrategy::checkWaveOnTheWay( const Coord3D *waveCenter )
{
	const AISkillSettings &sk = skill();
	if (!sk.m_useFightCheck || m_ai->isFeatureOff(AIPlayer::AIF_FIGHT))
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextWaveCheck || now - m_armyStateFrame < 4 * LOGICFRAMES_PER_SECOND)
		return;
	m_nextWaveCheck = now + 2 * LOGICFRAMES_PER_SECOND;
	// Once the wave is there, the teams weigh the fight they are in (evaluateTeam).
	if (dist2D(*waveCenter, m_waveObjective) < 350.0f)
	{
		m_waveBadSince = 0;
		return;
	}

	Real ourPower = 0.0f, theirPower = 0.0f;
	const Real advantage = forecastAdvantage(&m_waveObjective, 450.0f, &ourPower, &theirPower);
	if (advantage >= sk.m_pullbackAdvantage)
	{
		m_waveBadSince = 0;
		return;
	}
	if (m_waveBadSince == 0)
		m_waveBadSince = now;
	if (now - m_waveBadSince < secondsToFrames(sk.m_reactionSeconds + 3.0f))
		return;

	Coord3D rally;
	if (!rallyPoint(&rally))
		return;
	AI_TRACE("wave PULLS BACK: advantage %.2f (ours %.0f, theirs %.0f at (%.0f,%.0f))", advantage, ourPower, theirPower, m_waveObjective.x, m_waveObjective.y);
	++m_pullbacks;
	m_waveBadSince = 0;
	m_armyState = ARMY_GATHER;
	m_armyStateFrame = now;
	m_armyPeak = m_armyValue;
	m_armyGrowthFrame = now;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		AITeamRecord &rec = m_teams[i];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr || !isManageableTeam(team))
			continue;
		rec.m_mode = AITEAM_RETREATING;
		rec.m_modeFrame = now;
		rec.m_target = rally;
		rec.m_badSince = 0;
		orderTeamMove(team, &rally);
	}
}

//-------------------------------------------------------------------------------------------------
// team bookkeeping
//-------------------------------------------------------------------------------------------------
AITeamRecord *AIStrategy::findRecord( TeamID id, Bool create )
{
	for (Int i = 0; i < m_numTeams; ++i)
	{
		if (m_teams[i].m_team == id)
			return &m_teams[i];
	}
	if (!create || m_numTeams >= MAX_TEAMS)
		return nullptr;
	AITeamRecord &r = m_teams[m_numTeams++];
	memset(&r, 0, sizeof(r));
	r.m_team = id;
	r.m_mode = AITEAM_FREE;
	r.m_modeFrame = TheGameLogic->getFrame();
	r.m_lastAdvantage = 99.0f;
	return &r;
}

void AIStrategy::pruneRecords()
{
	Int out = 0;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr || !team->hasAnyUnits())
			continue;
		if (out != i)
			m_teams[out] = m_teams[i];
		++out;
	}
	m_numTeams = out;
}

/// Teams the strategic layer may command: field armies, not base guards, transports, workers or the default team.
Bool AIStrategy::isManageableTeam( Team *team ) const
{
	if (team == nullptr || !team->isActive())
		return FALSE;
	const TeamPrototype *proto = team->getPrototype();
	if (proto == nullptr || proto == m_player->getDefaultTeam()->getPrototype())
		return FALSE;
	const TeamTemplateInfo *info = proto->getTemplateInfo();
	if (info->m_isBaseDefense || info->m_isPerimeterDefense || !info->m_transportUnitType.isEmpty())
		return FALSE;

	Int combat = 0, other = 0;
	for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
	{
		Object *obj = it.cur();
		if (obj == nullptr || obj->isEffectivelyDead())
			continue;
		if (obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_STRUCTURE) ||
				obj->isKindOf(KINDOF_TRANSPORT))
			return FALSE;
		if (obj->getAI() == nullptr)
			return FALSE;
		if (isCombatUnit(obj))
			++combat;
		else
			++other;
	}
	return combat > 0 && combat >= other;
}

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::mergeOn() const
{
	return skill().m_useMerge && !m_ai->isFeatureOff(AIPlayer::AIF_MERGE);
}

void AIStrategy::orderTeamMove( Team *team, const Coord3D *pos )
{
	if (!team->hasAnyUnits())
		return;
	if (AITeamRecord *r = findRecord(team->getID(), FALSE))
		r->m_idMark = TheGameLogic->getObjectIDCounter();
	AIGroupPtr group = TheAI->createGroup();
	if (!group)
		return;
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(group);
#else
	team->getTeamAsAIGroup(group.Peek());
#endif
	group->groupMoveToPosition(pos, FALSE, CMD_FROM_AI);
}

void AIStrategy::orderTeamAttackMove( Team *team, const Coord3D *pos )
{
	if (!team->hasAnyUnits())
		return;
	if (AITeamRecord *r = findRecord(team->getID(), FALSE))
		r->m_idMark = TheGameLogic->getObjectIDCounter();
	AIGroupPtr group = TheAI->createGroup();
	if (!group)
		return;
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(group);
#else
	team->getTeamAsAIGroup(group.Peek());
#endif
	group->groupAttackMoveToPosition(pos, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
}

//-------------------------------------------------------------------------------------------------
// the field army
//
// The doctrine: the finished teams gather at a rally point in front of the base, where they fight as
// one body whatever comes (an enemy that arrives team by team meets all of it).  When the army is big
// enough for what has been seen of the enemy, all teams go to an objective together.  A team that is
// losing a fight pulls back to the rally point; when the wave is spent the army gathers again.
//-------------------------------------------------------------------------------------------------
/// Where the army gathers: in front of the base, on the side of the enemy.
Bool AIStrategy::rallyPoint( Coord3D *pos )
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return FALSE;

	// Recomputed from time to time: the first sightings of the enemy tell the direction better than the map.
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!m_rallySet || now >= m_nextRally)
	{
		m_nextRally = now + 10 * LOGICFRAMES_PER_SECOND;
		Coord3D toward;
		Bool haveTarget = enemyStartPosition(&toward);
		if (!haveTarget)
		{
			Region3D extent;
			TheTerrainLogic->getExtent(&extent);
			toward.x = (extent.lo.x + extent.hi.x) * 0.5f;
			toward.y = (extent.lo.y + extent.hi.y) * 0.5f;
			toward.z = 0.0f;
		}
		Real dx = toward.x - base.x, dy = toward.y - base.y;
		Real len = sqrtf(dx * dx + dy * dy);
		if (len < 1.0f)
		{
			dx = 1.0f; dy = 0.0f; len = 1.0f;
		}
		const Real out = m_ai->m_baseRadius + 140.0f;
		m_rally.x = base.x + dx / len * out;
		m_rally.y = base.y + dy / len * out;
		m_rally.z = TheTerrainLogic->getGroundHeight(m_rally.x, m_rally.y);
		m_rallySet = TRUE;
	}
	*pos = m_rally;
	return TRUE;
}

/// The army value the AI wants before it attacks: enough to beat what it has seen, and a minimum of its own.
Real AIStrategy::waveTarget() const
{
	const AISkillSettings &sk = skill();
	Real target = sk.m_waveSizeScale * (1.5f * m_enemy.armyValue() + 0.8f * (m_enemy.roleValue(AIROLE_DEFENCE) + m_enemy.roleValue(AIROLE_AIRDEFENCE)));
	if (target < sk.m_minWaveValue)
		target = sk.m_minWaveValue;
	return target;
}

void AIStrategy::updateTeams()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	pruneRecords();

	// Register every manageable team (new ones start under script control).
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (!isManageableTeam(team))
				continue;
			const Int before = m_numTeams;
			AITeamRecord *rec = findRecord(team->getID(), TRUE);
			// A team that has just been activated is taken in hand at once: the scripts send it hunting in the
			// same frame, and it would be far from the army by the time its turn comes.
			if (rec && m_numTeams > before)
				evaluateTeam(team, rec);
		}
	}

	if (!m_ai->isFeatureOff(AIPlayer::AIF_WAVE))
		updateArmy();
	updateScout();

	if (skill().m_useRetreat && !m_ai->isFeatureOff(AIPlayer::AIF_DEFEND))
		sendReinforcementsToThreat();

	if (m_numTeams == 0)
		return;

	// One team per decision: the attention of the player is limited.
	if (m_teamCursor >= m_numTeams)
		m_teamCursor = 0;
	AITeamRecord *rec = &m_teams[m_teamCursor++];
	Team *team = TheTeamFactory->findTeamByID(rec->m_team);
	if (team && isManageableTeam(team))
		evaluateTeam(team, rec);
	(void)now;
}

/**
 * Scouting: from time to time the fastest unit of the army goes to look at the enemy's base and comes
 * back.  What it sees feeds the enemy model, which sizes the attack waves and picks their objectives.
 */
void AIStrategy::updateScout()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (m_ai->isFeatureOff(AIPlayer::AIF_SCOUT))
		return;

	Object *scout = m_scoutID != INVALID_ID ? TheGameLogic->findObjectByID(m_scoutID) : nullptr;
	if (scout && scout->isEffectivelyDead())
		scout = nullptr;
	if (scout)
	{
		BodyModuleInterface *body = scout->getBodyModule();
		const Bool hurt = body && body->getMaxHealth() > 0.0f && body->getHealth() < 0.5f * body->getMaxHealth();
		if (dist2D(*scout->getPosition(), m_scoutTarget) < 200.0f || now > m_scoutUntil || hurt)
		{
			// Seen enough: home.
			Coord3D rally;
			if (rallyPoint(&rally) && scout->getAI())
				scout->getAI()->aiMoveToPosition(&rally, CMD_FROM_AI);
			AI_TRACE("scout %u returns", scout->getID());
			m_scoutID = INVALID_ID;
		}
		return;
	}
	m_scoutID = INVALID_ID;
	if (now < m_nextScout)
		return;
	m_nextScout = now + secondsToFrames(45.0f + (Real)GameLogicRandomValue(0, 30));

	// The target: where the enemy was last seen in numbers, else where it started.
	Coord3D target;
	Bool haveTarget = FALSE;
	Real bestValue = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role >= AIROLE_DEFENCE && c.m_role != AIROLE_ECONOMY)
		{
			const AICombatFigures *f = AICombatModel::figures(templateOf(c.m_templateID));
			if (f && f->m_cost > bestValue)
			{
				bestValue = f->m_cost;
				target = c.m_pos;
				haveTarget = TRUE;
			}
		}
	}
	if (!haveTarget && !enemyStartPosition(&target))
		return;

	// The fastest combat unit in a team that is not fighting, if it is clearly faster than the rest.
	Object *best = nullptr;
	Real bestSpeed = 0.0f, speedSum = 0.0f;
	Int units = 0;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr || m_teams[i].m_mode == AITEAM_RETREATING)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->getAI() == nullptr || !isCombatUnit(obj))
				continue;
			const Real speed = obj->getAI()->getCurLocomotorSpeed();
			speedSum += speed;
			++units;
			if (speed > bestSpeed)
			{
				bestSpeed = speed;
				best = obj;
			}
		}
	}
	if (best == nullptr || units < 2 || bestSpeed < 1.4f * (speedSum / units))
		return;

	m_scoutID = best->getID();
	m_scoutTarget = target;
	m_scoutUntil = now + secondsToFrames(45.0f);
	best->getAI()->aiMoveToPosition(&target, CMD_FROM_AI);
	AI_TRACE("scout %u goes to (%.0f,%.0f)", best->getID(), target.x, target.y);
}

/**
 * Can the army bring a base down?  The base to be taken is assumed to be about as big as our own (the
 * enemy's is not known until scouted); the army must be able to do that much damage to it within
 * a minute and a half.  The shortage steers what is built next and what the army waits for.
 */
void AIStrategy::updateSiege()
{
	// What a structure takes: our own structures, and the toughest one's armor for the damage tables.
	Real structureHealth = 0.0f;
	const AICombatFigures *armor = nullptr;
	Real armorHealth = 0.0f;
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
				if (obj == nullptr || !obj->isKindOf(KINDOF_STRUCTURE) || obj->isEffectivelyDead())
					continue;
				const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
				if (f == nullptr)
					continue;
				structureHealth += f->m_maxHealth;
				if (f->m_maxHealth > armorHealth)
				{
					armorHealth = f->m_maxHealth;
					armor = f;
				}
			}
		}
	}
	if (armor == nullptr)
		return;

	Real siegeDps = 0.0f;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj && !obj->isEffectivelyDead() && isCombatUnit(obj))
				siegeDps += AICombatModel::damagePerSecond(AICombatModel::figures(obj->getTemplate()), armor);
		}
	}
	const Real needed = 0.8f * structureHealth / 90.0f;
	m_siegeShortage = needed > 0.0f ? clampReal(1.0f - siegeDps / needed, 0.0f, 1.0f) : 0.0f;
	if (m_ai->isFeatureOff(AIPlayer::AIF_SIEGE))
		m_siegeShortage = 0.0f;
}

/// Decide when the army gathers and when it goes.
void AIStrategy::updateArmy()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();

	Real value = 0.0f;
	Coord3D center;
	center.zero();
	Real weight = 0.0f;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr)
			continue;
		const Real v = teamValue(team);
		const Coord3D *p = team->getEstimateTeamPosition();
		if (p)
		{
			center.x += p->x * v;
			center.y += p->y * v;
			weight += v;
		}
		value += v;
	}
	if (weight > 0.0f)
	{
		center.x /= weight;
		center.y /= weight;
	}
	m_armyValue = value;

	if (m_armyState == ARMY_GATHER)
	{
		if (value > m_armyPeak + 1.0f)
		{
			m_armyPeak = value;
			m_armyGrowthFrame = now;
		}
		if (weight <= 0.0f)
			return;
		const Real target = waveTarget();
		// An army that has stopped growing (all teams alive, money spent elsewhere) does not wait for ever.
		const Bool stalled = now - m_armyGrowthFrame >= secondsToFrames(sk.m_waveHoldSeconds);
		// The army must be able to take buildings down, too (or have waited for it long enough).
		const Bool equipped = m_siegeShortage <= 0.5f || now - m_armyGrowthFrame >= 2 * secondsToFrames(sk.m_waveHoldSeconds);
		if ((value >= target && equipped) || (stalled && value >= 0.6f * target && equipped))
		{
			if (m_launchBlockedSince != 0 && now < m_nextLaunchCheck)
				return;	// held back by the fight check: look again in a moment
			if (rollMistake())
			{
				m_armyGrowthFrame = now;	// a moment's hesitation
				return;
			}
			Coord3D objective;
			if (!chooseObjective(&center, value, &objective))
				return;
			if (!checkWaveLaunch(&objective))
				return;
			AI_TRACE("WAVE launches: value %.0f target %.0f siege shortage %.2f -> (%.0f,%.0f)", value, target, m_siegeShortage, objective.x, objective.y);
			m_armyState = ARMY_ATTACK;
			m_armyStateFrame = now;
			m_launchValue = value;
			m_waveObjective = objective;
			for (Int i = 0; i < m_numTeams; ++i)
				m_teams[i].m_inWave = TRUE;
			for (Int i = 0; i < m_numTeams; ++i)
			{
				AITeamRecord &rec = m_teams[i];
				Team *team = TheTeamFactory->findTeamByID(rec.m_team);
				if (team == nullptr || rec.m_mode == AITEAM_RETREATING)
					continue;
				rec.m_mode = AITEAM_ATTACKING;
				rec.m_modeFrame = now;
				rec.m_target = objective;
				rec.m_idleSince = 0;
				orderTeamAttackMove(team, &objective);
			}
		}
		else
		{
			m_launchBlockedSince = 0;
		}
	}
	else
	{
		// The wave is spent (most of it dead or run away): gather again.
		if (value >= 0.25f * m_launchValue && m_numTeams > 0 && weight > 0.0f)
			checkWaveOnTheWay(&center);
		if (m_armyState != ARMY_ATTACK)
			return;
		if (value < 0.25f * m_launchValue || (m_numTeams == 0))
		{
			AI_TRACE("wave spent: value %.0f of %.0f", value, m_launchValue);
			m_armyState = ARMY_GATHER;
			m_armyStateFrame = now;
			m_armyPeak = value;
			m_armyGrowthFrame = now;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::evaluateTeam( Team *team, AITeamRecord *rec )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();

	// Where is the team, how strong, how far can it shoot.
	Coord3D rally;
	const Bool haveRally = rallyPoint(&rally);
	Coord3D center, allCenter;
	center.zero();
	allCenter.zero();
	Int count = 0, idleCount = 0, allCount = 0, allIdleCount = 0;
	Int reserves = 0;
	Real maxRange = 0.0f;
	// Units that joined the team after its last order (reinforcements) are not a part of the force that is out
	// there: while the team is away they are kept out of its position, and sent to the rally point (see below).
	const Bool mergeUnits = haveRally && mergeOn() && rec->m_idMark != 0;
	for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
	{
		Object *obj = it.cur();
		if (obj == nullptr || obj->isEffectivelyDead())
			continue;
		const Bool idle = obj->getAI() && obj->getAI()->isIdle();
		allCenter.x += obj->getPosition()->x;
		allCenter.y += obj->getPosition()->y;
		++allCount;
		if (idle)
			++allIdleCount;
		const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
		if (f && f->m_range > maxRange)
			maxRange = f->m_range;
		if (mergeUnits && obj->getID() >= rec->m_idMark)
		{
			++reserves;
			continue;
		}
		center.x += obj->getPosition()->x;
		center.y += obj->getPosition()->y;
		++count;
		if (idle)
			++idleCount;
	}
	if (allCount == 0)
		return;
	if (reserves > 0 && count > 0)
	{
		center.x /= count;
		center.y /= count;
		if (dist2D(center, rally) > 500.0f)
		{
			// The team is out: the reinforcements wait at the rally point for the next wave.
			for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
			{
				Object *obj = it.cur();
				if (obj == nullptr || obj->isEffectivelyDead() || obj->getID() < rec->m_idMark || obj->getAI() == nullptr)
					continue;
				AIUpdateInterface *ai = obj->getAI();
				if (dist2D(*obj->getPosition(), rally) < 120.0f)
					continue;
				const Coord3D *goal = ai->getGoalPosition();
				if (ai->isIdle() || goal == nullptr || dist2D(*goal, rally) > 150.0f)
				{
					ai->aiMoveToPosition(&rally, CMD_FROM_AI);
					++m_mergedUnits;
				}
			}
		}
		else
		{
			count = allCount;
			idleCount = allIdleCount;
			center = allCenter;
			center.x /= count;
			center.y /= count;
		}
	}
	else
	{
		count = allCount;
		idleCount = allIdleCount;
		center = allCenter;
		center.x /= count;
		center.y /= count;
	}
	if (count == 0)
		return;
	center.z = TheTerrainLogic->getGroundHeight(center.x, center.y);

	if (!haveRally)
		rally = center;
	const Real rallyDist = dist2D(center, rally);
	const Bool allIdle = (idleCount == count);

	const Real radius = clampReal(maxRange + 200.0f, 300.0f, 600.0f);
	Real ourPower = 0.0f, theirPower = 0.0f;
	const Real advantage = fightAdvantage(&center, radius, &ourPower, &theirPower);
	rec->m_lastAdvantage = advantage;
	rec->m_lastCheck = now;
	const Bool contact = theirPower > 0.0f;

	const UnsignedInt reactionFrames = secondsToFrames(sk.m_reactionSeconds);

	// Losing?  Pull out - once the situation has looked bad for the reaction time of the player.  (Near
	// the rally point the army stands its ground: there is nowhere better to be.)
	if (rec->m_mode != AITEAM_RETREATING && rec->m_mode != AITEAM_REGROUPING)
	{
		if (sk.m_useRetreat && !m_ai->isFeatureOff(AIPlayer::AIF_RETREAT) && contact && rallyDist > 400.0f && advantage < sk.m_retreatAdvantage && theirPower >= 150.0f)
		{
			if (rec->m_badSince == 0)
				rec->m_badSince = now;
			if (now - rec->m_badSince >= reactionFrames && !rollMistake())
			{
				AI_TRACE("team %u RETREATS: advantage %.2f our %.0f their %.0f", team->getID(), advantage, ourPower, theirPower);
				rec->m_mode = AITEAM_RETREATING;
				rec->m_modeFrame = now;
				rec->m_target = rally;
				rec->m_badSince = 0;
				orderTeamMove(team, &rally);
				return;
			}
		}
		else
		{
			rec->m_badSince = 0;
		}
	}

	switch (rec->m_mode)
	{
		case AITEAM_FREE:
		case AITEAM_ATTACKING:
		case AITEAM_DEFENDING:
		{
			// In a fight that goes well (or is at the rally point anyway) the units fight on their own: focus
			// fire is in the target selection.  A team that is alone out there while the army gathers does not.
			if (contact && !(m_armyState == ARMY_GATHER && rec->m_mode == AITEAM_FREE && rallyDist > 400.0f && advantage < 1.2f))
			{
				rec->m_idleSince = 0;
				return;
			}

			// Arrived and nothing to do.
			if (rec->m_mode != AITEAM_FREE && (allIdle || dist2D(center, rec->m_target) < 120.0f))
			{
				rec->m_mode = AITEAM_FREE;
				rec->m_modeFrame = now;
			}
			if (rec->m_mode != AITEAM_FREE || m_ai->isFeatureOff(AIPlayer::AIF_WAVE))
				break;

			// A team that appeared while a wave is out is not part of it: it gathers at the rally point and joins the next one.
			const Bool waiting = m_armyState == ARMY_ATTACK && !rec->m_inWave && mergeOn();
			if (m_armyState == ARMY_GATHER || waiting)
			{
				// Bring the team to the army.  (Units that hunt or follow a script path are not idle, so this
				// also reins in teams that the scripts have sent out alone.)
				if (rallyDist > 240.0f && now - rec->m_orderFrame >= 10 * LOGICFRAMES_PER_SECOND)
				{
					rec->m_orderFrame = now;
					rec->m_target = rally;
					if (waiting)
						++m_mergedTeams;
					if (contact)
						orderTeamMove(team, &rally);	// not in a position to fight: get to the army
					else
						orderTeamAttackMove(team, &rally);
				}
			}
			else if (rallyDist > 300.0f && (allIdle || now - rec->m_orderFrame >= 20 * LOGICFRAMES_PER_SECOND))
			{
				// A wave is out: a team that has finished its objective in the field takes the next one.  (Teams
				// at home wait at the rally point and make up the next wave together; sent one by one they
				// would be fed to the enemy.)
				Coord3D objective;
				if (chooseObjective(&center, m_armyValue, &objective))
				{
					rec->m_mode = AITEAM_ATTACKING;
					rec->m_modeFrame = now;
					rec->m_orderFrame = now;
					rec->m_target = objective;
					orderTeamAttackMove(team, &objective);
				}
				else
				{
					m_armyState = ARMY_GATHER;
					m_armyStateFrame = now;
				}
			}
			break;
		}

		case AITEAM_RETREATING:
		{
			if (rallyDist < 250.0f || now - rec->m_modeFrame > 25 * LOGICFRAMES_PER_SECOND)
			{
				rec->m_mode = AITEAM_REGROUPING;
				rec->m_modeFrame = now;
			}
			else if (allIdle)
			{
				orderTeamMove(team, &rec->m_target);
			}
			break;
		}

		case AITEAM_REGROUPING:
		{
			// Rest a while at the rally point (units that repair themselves or get repaired recover), then
			// the team is part of the army again.
			if (now - rec->m_modeFrame >= secondsToFrames(12.0f) && !contact)
			{
				rec->m_mode = AITEAM_FREE;
				rec->m_modeFrame = now;
				rec->m_orderFrame = 0;
			}
			break;
		}
	}
}

/// Value of our other field teams near a place: the army an objective can count on to follow up.
Real AIStrategy::alliedValueNear( const Coord3D *center, Team *except ) const
{
	Real v = 0.0f;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *other = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (other == nullptr || other == except)
			continue;
		const Coord3D *p = other->getEstimateTeamPosition();
		if (p && dist2D(*p, *center) < 700.0f)
			v += teamValue(other);
	}
	return v;
}

//-------------------------------------------------------------------------------------------------
/**
 * While a wave is out, enemies in our base: bring the field teams home unless they are in the middle of
 * a fight that goes well.  (While the army gathers, it is at the base anyway.)
 */
void AIStrategy::sendReinforcementsToThreat()
{
	if (m_armyState != ARMY_ATTACK)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return;
	const Real zone = m_ai->m_baseRadius + 250.0f;

	// Armed enemy units seen in the base in the last few seconds.
	Real threat = 0.0f;
	Coord3D where;
	where.zero();
	Real best = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 4 * LOGICFRAMES_PER_SECOND)
			continue;
		if (dist2D(c.m_pos, base) > zone)
			continue;
		const AICombatFigures *f = AICombatModel::figures(templateOf(c.m_templateID));
		if (f == nullptr || !f->m_armed)
			continue;
		threat += f->m_cost;
		if (f->m_cost > best)
		{
			best = f->m_cost;
			where = c.m_pos;
		}
	}

	if (threat < 400.0f)
	{
		m_threatSince = 0;
		return;
	}
	if (m_threatSince == 0)
		m_threatSince = now;
	m_threatPos = where;
	if (now - m_threatSince < secondsToFrames(skill().m_reactionSeconds))
		return;
	if (now - m_lastThreatResponse < 8 * LOGICFRAMES_PER_SECOND)
		return;
	m_lastThreatResponse = now;

	// Is there anything at home to deal with it?
	if (armyValueNear(&base, zone + 200.0f, FALSE) >= 0.8f * threat)
		return;

	AI_TRACE("base threatened (%.0f at (%.0f,%.0f)): recalling the army", threat, where.x, where.y);
	m_armyState = ARMY_GATHER;
	m_armyStateFrame = now;
	m_armyPeak = m_armyValue;
	m_armyGrowthFrame = now;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		AITeamRecord &rec = m_teams[i];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr || !isManageableTeam(team))
			continue;
		rec.m_mode = AITEAM_DEFENDING;
		rec.m_modeFrame = now;
		rec.m_target = where;
		rec.m_idleSince = 0;
		orderTeamAttackMove(team, &where);
	}
}

//-------------------------------------------------------------------------------------------------
// economy
//-------------------------------------------------------------------------------------------------
Int AIStrategy::extraGatherers() const
{
	if (!skill().m_expandEconomy)
		return 0;
	const UnsignedInt minutes = TheGameLogic->getFrame() / (LOGICFRAMES_PER_SECOND * 60);
	Int extra = 0;
	if (minutes >= 4 && m_player->getMoney()->countMoney() < TheAI->getAiData()->m_resourcesWealthy)
		extra = 1;
	if (minutes >= 9)
		extra += 1;
	return extra;
}

void AIStrategy::updateEconomy()
{
	// Idle production buildings while there is money: shorten the wait for the next team.
	Int idle = 0;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		Object *obj = TheGameLogic->findObjectByID(info->getObjectID());
		if (obj == nullptr || obj->getControllingPlayer() != m_player)
			continue;
		if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD))
			continue;
		ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		if (pu == nullptr || obj->isKindOf(KINDOF_FS_SUPPLY_CENTER))
			continue;
		if (pu->getProductionCount() == 0)
			++idle;
	}
	const Int money = m_player->getMoney()->countMoney();
	m_productionStarved = (idle > 0 && money >= 250 && !m_ai->isFeatureOff(AIPlayer::AIF_STARVE));

	if (skill().m_expandEconomy)
		tryExpand();
}

void AIStrategy::tryExpand()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt minutes = now / (LOGICFRAMES_PER_SECOND * 60);
	if (minutes < 3 || now - m_lastExpandFrame < 90 * LOGICFRAMES_PER_SECOND)
		return;
	if (m_expansions >= 1 + (Int)(minutes / 6) || m_expansions >= 3)
		return;
	if (m_player->getAttackedFrame() + 20 * LOGICFRAMES_PER_SECOND > now)
		return;

	// The supply center template is whatever our build list uses for it.
	AsciiString centerName;
	Int centers = 0;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (!info->isSupplyBuilding())
			continue;
		if (centerName.isEmpty())
			centerName = info->getTemplateName();
		if (TheGameLogic->findObjectByID(info->getObjectID()) == nullptr && info->isPriorityBuild())
			return;	// one is already being planned
		if (TheGameLogic->findObjectByID(info->getObjectID()) != nullptr)
			++centers;
	}
	if (centerName.isEmpty())
		return;
	const ThingTemplate *center = TheThingFactory->findTemplate(centerName);
	if (center == nullptr)
		return;
	if (m_player->getMoney()->countMoney() < 2 * (Int)center->calcCostToBuild(m_player))
		return;

	Object *source = m_ai->findSupplyCenter(1500);
	if (source == nullptr || !m_ai->isLocationSafe(source->getPosition(), center))
		return;
	m_ai->buildBySupplies(1500, centerName);
	m_lastExpandFrame = now;
	++m_expansions;
}

//-------------------------------------------------------------------------------------------------
// superweapons
//-------------------------------------------------------------------------------------------------
void AIStrategy::updatePowers()
{
	// Superweapon structures that have been ready for a while without a script firing them.
	const UnsignedInt now = TheGameLogic->getFrame();
	Bool anyReady = FALSE;

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
				if (obj == nullptr || !obj->isKindOf(KINDOF_FS_SUPERWEAPON) || obj->isEffectivelyDead())
					continue;
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->isDisabled())
					continue;

				BehaviorModule **modules = obj->getBehaviorModules();
				for (; modules && *modules; ++modules)
				{
					SpecialPowerModuleInterface *sp = (*modules)->getSpecialPower();
					if (sp == nullptr || !sp->isReady())
						continue;
					const SpecialPowerTemplate *power = sp->getSpecialPowerTemplate();
					if (power == nullptr || power->getRadiusCursorRadius() <= 0.0f)
						continue;
					anyReady = TRUE;
					if (m_powerReadySince == 0)
						m_powerReadySince = now;
					// Give the scripts a few seconds to fire it themselves.
					if (now - m_powerReadySince < 15 * LOGICFRAMES_PER_SECOND)
						continue;

					Player *enemy = enemyPlayer();
					if (enemy == nullptr)
						continue;
					Coord3D target;
					Real radius = power->getRadiusCursorRadius();
					if (radius < 50.0f)
						radius = 50.0f;
					if (!m_player->computeSuperweaponTarget(power, &target, enemy->getPlayerIndex(), radius))
						continue;
					if (target.lengthSqr() <= 0.0f)
						continue;
					sp->doSpecialPowerAtLocation(&target, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT);
					m_powerReadySince = 0;
					return;
				}
			}
		}
	}
	if (!anyReady)
		m_powerReadySince = 0;
}

//-------------------------------------------------------------------------------------------------
/**
 * Best place to hit.  Candidate centers are the known objects of the victim, each scored by the value
 * inside the blast, with our own and allied things counting against it.  Only what the player has seen
 * counts: structures once seen stay known, units only while visible.
 */
Bool AIStrategy::computeSuperweaponTarget( const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius )
{
	Player *victim = ThePlayerList->getNthPlayer(playerNdx);
	if (victim == nullptr)
		return FALSE;
	if (weaponRadius < 20.0f)
		weaponRadius = 20.0f;

	struct Known
	{
		Coord3D pos;
		Real value;
	};
	enum { MAX_KNOWN = 160, MAX_FRIENDS = 80 };
	Known known[MAX_KNOWN];
	Int numKnown = 0;
	Known friends[MAX_FRIENDS];
	Int numFriends = 0;

	const Int me = m_player->getPlayerIndex();
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || obj->isOffMap() || obj->isContained())
			continue;
		const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
		if (f == nullptr || obj->isKindOf(KINDOF_UNATTACKABLE) || obj->isKindOf(KINDOF_INERT) || obj->isKindOf(KINDOF_PROJECTILE) ||
				obj->isKindOf(KINDOF_SHRUBBERY) || obj->isKindOf(KINDOF_PROP))
			continue;

		Player *owner = obj->getControllingPlayer();
		if (owner == victim)
		{
			const ObjectShroudStatus s = obj->getShroudedStatus(me);
			const Bool seen = (s == OBJECTSHROUD_CLEAR || s == OBJECTSHROUD_PARTIAL_CLEAR) ||
				(s == OBJECTSHROUD_FOGGED && obj->isKindOf(KINDOF_STRUCTURE));
			if (!seen)
				continue;
			if (obj->isKindOf(KINDOF_AIRCRAFT) && obj->isSignificantlyAboveTerrain())
				continue;	// planes in the air cannot be hit by ground blasts

			Real value = f->m_cost;
			if (obj->isKindOf(KINDOF_COMMANDCENTER) || obj->isKindOf(KINDOF_FS_SUPERWEAPON))
				value *= 0.1f;	// too tough for a blast to finish: not worth aiming at
			else if (obj->isKindOf(KINDOF_FS_FACTORY) || obj->isKindOf(KINDOF_FS_BARRACKS) || obj->isKindOf(KINDOF_FS_WARFACTORY) ||
							 obj->isKindOf(KINDOF_FS_AIRFIELD) || obj->isKindOf(KINDOF_FS_POWER))
				value *= 1.3f;
			else if (obj->isKindOf(KINDOF_HARVESTER))
				value *= 1.4f;
			if (numKnown < MAX_KNOWN)
			{
				known[numKnown].pos = *obj->getPosition();
				known[numKnown].value = value;
				++numKnown;
			}
		}
		else if (owner == m_player || m_player->getRelationship(obj->getTeam()) == ALLIES)
		{
			if (numFriends < MAX_FRIENDS)
			{
				friends[numFriends].pos = *obj->getPosition();
				friends[numFriends].value = f->m_cost;
				++numFriends;
			}
		}
	}

	if (numKnown == 0)
	{
		// Nothing seen yet: the enemy base location is on the map.
		return enemyStartPosition(pos);
	}

	// Score a center: value inside the blast (full at the center, half at the edge), allies count triple against it.
	struct Scorer
	{
		const Known *k; Int nk; const Known *fr; Int nf; Real r;
		Real operator()(Real x, Real y) const
		{
			Real s = 0.0f;
			for (Int i = 0; i < nk; ++i)
			{
				const Real d = sqrtf((k[i].pos.x - x) * (k[i].pos.x - x) + (k[i].pos.y - y) * (k[i].pos.y - y));
				if (d < r)
					s += k[i].value * (1.0f - 0.5f * d / r);
			}
			for (Int i = 0; i < nf; ++i)
			{
				const Real d = sqrtf((fr[i].pos.x - x) * (fr[i].pos.x - x) + (fr[i].pos.y - y) * (fr[i].pos.y - y));
				if (d < r)
					s -= 3.0f * fr[i].value * (1.0f - 0.5f * d / r);
			}
			return s;
		}
	};
	Scorer score = { known, numKnown, friends, numFriends, weaponRadius };

	Real bestScore = -1.0f;
	Coord3D best;
	best.zero();
	for (Int i = 0; i < numKnown; ++i)
	{
		const Real s = score(known[i].pos.x, known[i].pos.y);
		if (s > bestScore)
		{
			bestScore = s;
			best = known[i].pos;
		}
	}
	if (bestScore <= 0.0f)
		return FALSE;

	// Fine tune: slide the center to the best of its neighbours while it helps.
	Real step = weaponRadius * 0.3f;
	for (Int pass = 0; pass < 6; ++pass)
	{
		Bool moved = FALSE;
		static const Real dx[8] = { 1, -1, 0, 0, 0.7f, -0.7f, 0.7f, -0.7f };
		static const Real dy[8] = { 0, 0, 1, -1, 0.7f, 0.7f, -0.7f, -0.7f };
		for (Int d = 0; d < 8; ++d)
		{
			const Real s = score(best.x + dx[d] * step, best.y + dy[d] * step);
			if (s > bestScore)
			{
				bestScore = s;
				best.x += dx[d] * step;
				best.y += dy[d] * step;
				moved = TRUE;
			}
		}
		if (!moved)
			step *= 0.5f;
	}

	best.z = TheTerrainLogic->getGroundHeight(best.x, best.y);
	*pos = best;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// snapshot
//-------------------------------------------------------------------------------------------------
void AIStrategy::crc( Xfer *xfer )
{
}

void AIStrategy::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	m_enemy.xfer(xfer);
	xfer->xferUnsignedInt(&m_nextScan);
	xfer->xferUnsignedInt(&m_nextTeamEval);
	xfer->xferUnsignedInt(&m_nextEconomy);
	xfer->xferUnsignedInt(&m_nextPowers);
	xfer->xferInt(&m_teamCursor);
	xfer->xferInt(&m_numTeams);
	xfer->xferUser(m_teams, sizeof(m_teams));
	xfer->xferInt(&m_armyState);
	xfer->xferUnsignedInt(&m_armyStateFrame);
	xfer->xferReal(&m_armyValue);
	xfer->xferReal(&m_armyPeak);
	xfer->xferUnsignedInt(&m_armyGrowthFrame);
	xfer->xferReal(&m_launchValue);
	xfer->xferCoord3D(&m_waveObjective);
	xfer->xferCoord3D(&m_rally);
	xfer->xferBool(&m_rallySet);
	xfer->xferUnsignedInt(&m_nextRally);
	xfer->xferBool(&m_productionStarved);
	xfer->xferUnsignedInt(&m_lastExpandFrame);
	xfer->xferInt(&m_expansions);
	xfer->xferUnsignedInt(&m_threatSince);
	xfer->xferCoord3D(&m_threatPos);
	xfer->xferUnsignedInt(&m_lastThreatResponse);
	xfer->xferUnsignedInt(&m_powerReadySince);
	xfer->xferReal(&m_siegeShortage);
	xfer->xferObjectID(&m_scoutID);
	xfer->xferUnsignedInt(&m_scoutUntil);
	xfer->xferUnsignedInt(&m_nextScout);
	xfer->xferCoord3D(&m_scoutTarget);
	xfer->xferUnsignedInt(&m_savingSince);
	xfer->xferUnsignedInt(&m_noSavingUntil);
	xfer->xferUser(m_ledger, sizeof(m_ledger));
	xfer->xferInt(&m_numSteps);
	xfer->xferUser(m_steps, sizeof(m_steps));
	xfer->xferInt(&m_tacticTeam);
	xfer->xferInt(&m_tacticUnit);
	xfer->xferUnsignedInt(&m_launchBlockedSince);
	xfer->xferUnsignedInt(&m_nextLaunchCheck);
	xfer->xferUnsignedInt(&m_waveBadSince);
	xfer->xferUnsignedInt(&m_nextWaveCheck);
}

void AIStrategy::loadPostProcess()
{
}
