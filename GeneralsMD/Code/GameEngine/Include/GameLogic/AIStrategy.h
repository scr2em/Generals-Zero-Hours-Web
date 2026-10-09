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

// AIStrategy.h
// The strategic layer of the skirmish computer player: what it knows about the enemy, how it
// picks counters, when and where it attacks, when it pulls back, how it spends its money.
//
// Everything here is part of the simulation and must stay deterministic: it only reads
// synchronised game state, uses GameLogicRandomValue for chance, never iterates a container keyed
// by a pointer, and keeps all of its memory in plain arrays that are saved with the game.

#pragma once

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include "Common/Team.h"
#include "GameLogic/AI.h"

class AIPlayer;
class Object;
class Player;
class Team;
class TeamPrototype;
class ThingTemplate;
class TAiData;
class SpecialPowerTemplate;
struct AISkillSettings;

//-------------------------------------------------------------------------------------------------
/// What an object does on the battlefield, as far as the enemy model is concerned.
enum AIRole CPP_11(: Int)
{
	AIROLE_INFANTRY = 0,		///< soldiers
	AIROLE_VEHICLE,					///< ground vehicles (including huge ones)
	AIROLE_AIRCRAFT,				///< planes and helicopters
	AIROLE_DEFENCE,					///< structures that shoot ground targets
	AIROLE_AIRDEFENCE,			///< structures that shoot aircraft
	AIROLE_PRODUCTION,			///< factories and command centers
	AIROLE_ECONOMY,					///< supply centers, harvesters, workers
	AIROLE_STRUCTURE,				///< every other structure
	AIROLE_COUNT
};

//-------------------------------------------------------------------------------------------------
/**
 * The combat figures of a thing template, distilled once from the weapon, armor and body data.
 * A pure function of the game data, so every client computes the same numbers.
 */
struct AICombatFigures
{
	Bool		m_valid;
	Bool		m_armed;						///< has a weapon that does damage
	Bool		m_airborne;					///< flies
	Bool		m_structure;
	Bool		m_infantry;
	Bool		m_canHitGround;
	Bool		m_canHitAir;
	AIRole	m_role;
	Real		m_maxHealth;
	Real		m_cost;							///< build cost, or a stand-in when the thing cannot be built
	Real		m_range;						///< longest weapon range
	Real		m_minRange;					///< shortest range of the longest-range weapon
	Real		m_speed;						///< ground speed in world units per second (0: immobile)
	Int			m_reloadFrames;			///< frames between two volleys of the longest range weapon
	Int			m_supportLevel;			///< 2: heals or repairs others, 1: builds and repairs structures (workers), 0: neither

	enum { MAX_WEAPONS = 3 };
	struct Weapon
	{
		Real	m_damage;						///< damage of one shot at the center (primary plus a share of the splash)
		Real	m_shotsPerSecond;		///< averaged over the clip and reload
		Real	m_splashRadius;
		Int		m_damageType;				///< DamageType
		Int		m_antiMask;					///< WeaponAntiMaskType bits
	};
	Int			m_numWeapons;
	Weapon	m_weapons[MAX_WEAPONS];
	const class ArmorTemplate *m_armor;
};

//-------------------------------------------------------------------------------------------------
/**
 * Pair-wise combat estimates between thing templates ("how well does a tank do against a rifleman"),
 * from weapon damage types against armor tables, range and cost.
 */
class AICombatModel
{
public:
	static void reset();																		///< forget the cache (new game, data overrides)
	static const AICombatFigures *figures(const ThingTemplate *tt);

	/// Damage per second that a does to d, ignoring range, in absolute damage points.
	static Real damagePerSecond(const AICombatFigures *a, const AICombatFigures *d);

	/// Fraction of one d-unit that one a-unit destroys every second.
	static Real killRate(const AICombatFigures *a, const AICombatFigures *d);

	/**
	 * How much more enemy value one a-unit destroys per second than it loses to d (cost-weighted
	 * exchange), as a score in (-1, 1): 0 is an even trade, positive means a counters d.
	 */
	static Real matchup(const AICombatFigures *a, const AICombatFigures *d);
};

//-------------------------------------------------------------------------------------------------
/// One enemy object we have seen.
struct AIContact
{
	ObjectID			m_id;
	UnsignedShort	m_templateID;
	Short					m_owner;						///< player index
	UnsignedByte	m_role;							///< AIRole
	UnsignedByte	m_healthPct;				///< 0..100 when last seen
	UnsignedInt		m_lastSeen;					///< frame
	Coord3D				m_pos;
};

/// Enemy forces by thing template, summed over the live contacts.
struct AIComposition
{
	UnsignedShort	m_templateID;
	Int						m_count;
	Real					m_value;
};

/**
 * What the computer player has seen of its enemies.  Fed only from objects that are currently
 * visible to it (not shrouded, not stealthed), plus the memory of those sightings.  Nothing is
 * read from objects the player cannot see.
 */
class AIEnemyModel
{
public:
	enum { MAX_CONTACTS = 160, MAX_COMPOSITION = 24 };

	AIEnemyModel();
	void reset();

	/// Look at a slice of the object list (resumes where the last call stopped).  Returns true when a full pass completed.
	Bool scan(Player *me, Int maxObjects);

	/// Drop contacts that are too old; recompute the summary.  Cheap.
	void refresh(Player *me, UnsignedInt now);

	const AIContact *contacts() const { return m_contacts; }
	Int numContacts() const { return m_numContacts; }

	Real roleValue(AIRole r) const { return m_roleValue[r]; }
	Int roleCount(AIRole r) const { return m_roleCount[r]; }
	Real armyValue() const { return m_roleValue[AIROLE_INFANTRY] + m_roleValue[AIROLE_VEHICLE] + m_roleValue[AIROLE_AIRCRAFT]; }
	Real totalValue() const;
	Real armedValue() const { return m_armedValue; }
	UnsignedInt lastScanFrame() const { return m_lastScanFrame; }

	const AIComposition *composition() const { return m_comp; }
	Int numComposition() const { return m_numComp; }

	void xfer(Xfer *xfer);

private:
	void noteObject(Object *obj, Player *me, UnsignedInt now);
	void rebuildSummary(UnsignedInt now);

	AIContact		m_contacts[MAX_CONTACTS];
	Int					m_numContacts;
	ObjectID		m_scanResume;				///< object id the incremental scan resumes at
	UnsignedInt	m_lastScanFrame;
	UnsignedInt	m_scanStartFrame;

	Real				m_roleValue[AIROLE_COUNT];
	Int					m_roleCount[AIROLE_COUNT];
	Real				m_armedValue;
	AIComposition	m_comp[MAX_COMPOSITION];
	Int					m_numComp;
};

//-------------------------------------------------------------------------------------------------
/// What the strategic AI is doing with one of its teams.
enum AITeamMode CPP_11(: Int)
{
	AITEAM_FREE = 0,				///< the scripts are in charge
	AITEAM_RETREATING,			///< pulling back from a losing fight
	AITEAM_REGROUPING,			///< at the rally point, waiting to be strong again
	AITEAM_DEFENDING,				///< sent to defend something of ours
	AITEAM_ATTACKING				///< sent by the strategic AI to an objective
};

struct AITeamRecord
{
	TeamID				m_team;
	Int						m_mode;							///< AITeamMode
	UnsignedInt		m_modeFrame;				///< when the mode was entered
	UnsignedInt		m_badSince;					///< frame since which the fight looked lost (0 = not)
	UnsignedInt		m_lastCheck;				///< frame of the last evaluation
	UnsignedInt		m_idleSince;				///< frame since which every member idles (0 = not idle)
	UnsignedInt		m_orderFrame;				///< frame of the last order the strategic layer gave the team
	Coord3D				m_target;						///< retreat point or objective
	Real					m_lastAdvantage;
};

/// Damage that units have just assigned to a target (split fire): a small table with expiry.
struct AILedgerEntry
{
	ObjectID			m_target;
	Real					m_damage;
	UnsignedInt		m_expire;
};

/// A unit that is stepping out of the fight for a moment (kiting): where it was attacking, and how to resume.
struct AIStepRecord
{
	ObjectID			m_unit;
	ObjectID			m_victim;
	UnsignedInt		m_until;						///< end of the current phase
	Int						m_phase;						///< 0: stepping away, 1: attacking again
	Bool					m_hasResume;				///< the team was on its way somewhere: go on there once the target is dead
	Coord3D				m_resume;
};

enum AIArmyState CPP_11(: Int)
{
	ARMY_GATHER = 0,				///< teams gather at the rally point and grow
	ARMY_ATTACK							///< a wave is out
};

//-------------------------------------------------------------------------------------------------
class AIStrategy : public Snapshot
{
public:
	AIStrategy( AIPlayer *ai, Player *p );
	~AIStrategy();

	void newMap();
	void update();																				///< once per logic frame; spreads its work over frames

	/// The Expert level settings (AIData.ini "ExpertSkill", or the code defaults).
	const AISkillSettings &skill() const;
	/// A deliberate slip: true with the mistake chance of the skill settings.
	Bool rollMistake() const;

	// ---- intel -------------------------------------------------------------------------------
	const AIEnemyModel &enemy() const { return m_enemy; }

	/// Where to attack next: the enemy objective with the best value, weighted by distance and the force defending it.
	Bool chooseObjective( const Coord3D *from, Real ourPower, Coord3D *objective );

	// ---- production --------------------------------------------------------------------------
	/// Log2 weight of a team against the observed enemy army: positive if it counters it.  0 when nothing is known.
	Real teamCounterScore( const TeamPrototype *proto ) const;
	/// Selection weight of a team: its production priority step below the best (hiPri) and how well it counters the enemy.
	Real teamWeight( const TeamPrototype *proto, Int hiPri ) const;
	/// A team that cannot be afforded yet is much better than what could be: wait for it?  False when the money is too far off.
	Bool shouldSaveFor( const TeamPrototype *proto, Real cost );
	void noteTeamPicked() { m_savingSince = 0; }
	/// True when production buildings idle while money piles up (shorten the team timer then).
	Bool productionIsStarved() const { return m_productionStarved; }
	/// Extra resource gatherers the economy can use in the current phase of the game.
	Int extraGatherers() const;

	// ---- waves -------------------------------------------------------------------------------
	/// Value (cost) of the living units of a team.
	static Real teamValue( Team *team );

	// ---- superweapons ------------------------------------------------------------------------
	Bool computeSuperweaponTarget( const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius );

	// ---- misc --------------------------------------------------------------------------------
	/// The enemy player the strategic AI is working against (skirmish: its current enemy).
	Player *enemyPlayer() const;
	Bool enemyStartPosition( Coord3D *pos ) const;

	Real armyValueNear( const Coord3D *pos, Real radius, Bool enemies ) const;

	// ---- tactics (AITactics.cpp) -------------------------------------------------------------
	/// Damage that units picking 'target' a moment ago will deal to it (split fire).
	Real assignedDamage( ObjectID target ) const;
	/// Note that a unit has picked 'target' and will deal about 'damage' to it in the next seconds.
	/// 'flags' (AIPlayer::PICK_...) says what the target selection did, for the statistics of the trace.
	void assignDamage( ObjectID target, Real damage, Int flags );
	/// Unit level tactics, spread over the frames: kiting.
	void updateTactics();

protected:
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

private:
	enum { MAX_TEAMS = 24 };

	// scheduling
	void updateEconomy();
	void updateTeams();
	void updatePowers();
	UnsignedInt secondsToFrames( Real seconds ) const;

	// teams
	AITeamRecord *findRecord( TeamID id, Bool create );
	void pruneRecords();
	Bool isManageableTeam( Team *team ) const;
	void evaluateTeam( Team *team, AITeamRecord *rec );
	Real fightAdvantage( const Coord3D *center, Real radius, Real *ourPower, Real *theirPower ) const;
	void orderTeamMove( Team *team, const Coord3D *pos );
	void orderTeamAttackMove( Team *team, const Coord3D *pos );
	void sendReinforcementsToThreat();
	void updateScout();
	void updateSiege();
	void updateArmy();
	Bool rallyPoint( Coord3D *pos );
	Real waveTarget() const;
	Real alliedValueNear( const Coord3D *center, Team *except ) const;

	// tactics (AITactics.cpp)
	enum { MAX_STEPS = 16 };
	AIStepRecord *findStep( ObjectID unit );
	void dropStep( AIStepRecord *rec );
	void updateSteps();
	void unitTactics( Object *unit, AITeamRecord *team );
	Bool planKite( Object *unit, Object *victim, const AITeamRecord *team, Coord3D *to, UnsignedInt *until );
	Bool enemyCanSee( const Object *victim ) const;

	// economy
	void tryExpand();

private:
	AIPlayer			*m_ai;
	Player				*m_player;

	AIEnemyModel	m_enemy;

	UnsignedInt		m_nextScan;
	UnsignedInt		m_nextTeamEval;
	UnsignedInt		m_nextEconomy;
	UnsignedInt		m_nextPowers;
	Int						m_teamCursor;			///< which team record is evaluated next

	AITeamRecord	m_teams[MAX_TEAMS];
	Int						m_numTeams;
	Int						m_armyState;			///< AIArmyState
	UnsignedInt		m_armyStateFrame;
	Real					m_armyValue;
	Real					m_armyPeak;
	UnsignedInt		m_armyGrowthFrame;
	Real					m_launchValue;
	Coord3D				m_waveObjective;
	Coord3D				m_rally;
	Bool					m_rallySet;
	UnsignedInt		m_nextRally;

	Bool					m_productionStarved;
	UnsignedInt		m_lastExpandFrame;
	Int						m_expansions;
	UnsignedInt		m_threatSince;		///< frame since which enemies are near our structures (0 = none)
	Coord3D				m_threatPos;
	UnsignedInt		m_lastThreatResponse;
	UnsignedInt		m_powerReadySince;
	Real					m_siegeShortage;	///< 0..1: how far the army is from being able to bring down a base in good time
	ObjectID			m_scoutID;				///< the unit sent to look at the enemy
	UnsignedInt		m_scoutUntil;
	UnsignedInt		m_nextScout;
	Coord3D				m_scoutTarget;
	UnsignedInt		m_savingSince;
	UnsignedInt		m_noSavingUntil;
	enum { LEDGER_SIZE = 32 };
	AILedgerEntry	m_ledger[LEDGER_SIZE];		///< split fire: damage assigned to targets, see assignDamage
	AIStepRecord	m_steps[MAX_STEPS];				///< units that are kiting
	Int						m_numSteps;
	Int						m_tacticTeam;							///< round robin over the units of the field teams
	Int						m_tacticUnit;
	Int						m_kiteStarts;							///< statistics for the trace
	Int						m_kiteResumes;
	Int						m_kiteRejectFast;
	Int						m_kiteRejectCorner;

	Int						m_splitPicks;			///< statistics for the trace: target picks, and picks changed by split fire, threat rules ...
	Int						m_splitSwitches;
	Int						m_threatSwitches;
	Int						m_supportPicks;
	Int						m_longRangePicks;
	Bool					m_trace;					///< print decisions (test bench: variant "trace")
	UnsignedInt		m_nextStatus;
};
