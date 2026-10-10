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
#include "GameLogic/AIProtect.h"
#include "GameLogic/AIRoute.h"
#include "GameLogic/AITacticsCore.h"

class AIPlayer;
class Object;
class Player;
class Team;
class TeamPrototype;
class ThingTemplate;
class TAiData;
class SpecialPowerTemplate;
class CommandButton;
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
/// A way into the base found from the terrain (AIGeo.cpp).
struct AIEntrance
{
	Coord3D	m_cross;			///< where the way in crosses the perimeter
	Coord3D	m_choke;			///< narrowest place on the way in (the crossing itself on open ground)
	Coord3D	m_rally;			///< a place just inside the base on the way in: where the army waits
	Real		m_width;			///< passable span at the chokepoint
	Real		m_prior;			///< expected share of the attacks from the paths of the enemy
	Real		m_weight;			///< the prior mixed with what the enemy model has seen
	Real		m_seen;				///< value of enemy ground units seen near the way in (decays)
	Int			m_defences;		///< defence structures placed for it
	Bool		m_choked;			///< a narrow place was found
	Bool		m_bridge;			///< the way in crosses a bridge
};
struct GeoPoly;

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
	Real		m_maxSplash;				///< largest blast radius of the weapons (poison and radiation count as a cloud of 40)
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
	/// Value of the armed units of our allies (other players of our team) seen in the last full pass over the objects.
	Real allyValue() const { return m_allyValue; }
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
	Real				m_allyValue;				///< armed allied units, as of the last complete pass
	Real				m_scanAlly;					///< the same, summed in the pass that is under way
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
	Bool					m_baseDefence;			///< sent by the base defence: not to be sent back to the rally point until the base is clear
	Bool					m_inWave;						///< part of the wave that is out (set at the launch); a team that appears later waits for the next one
	ObjectID			m_idMark;						///< object ids from here on belong to units that joined after the last order of the strategic layer (0 = none given)
};

/// A unit that is stepping out of the fight for a moment (kiting): where it was attacking, and how to resume.
struct AIStepRecord
{
	ObjectID			m_unit;
	ObjectID			m_victim;
	UnsignedInt		m_until;						///< end of the current phase
	Int						m_phase;						///< 0: stepping away, 1: attacking again
};

/// A unit on its way to a repair or heal pad (or a dozer to a structure): it is not part of its team until it is mended.
struct AIPatient
{
	ObjectID			m_unit;
	ObjectID			m_site;							///< the pad, or the structure for a dozer
	Int						m_kind;							///< 0: vehicle to a repair pad, 1: infantry to a heal pad, 2: dozer to a structure
	Int						m_phase;						///< 0: going and being mended, 1: on its way back
	UnsignedInt		m_start;
	UnsignedInt		m_phaseFrame;
	UnsignedInt		m_lastOrder;
	Int						m_retries;
};

/// An infantryman on his way into (or inside) one of our structures to hold it against an attack.
struct AIGarrisonRecord
{
	ObjectID			m_unit;
	ObjectID			m_building;
	UnsignedInt		m_since;
	Int						m_phase;						///< 0: entering, 1: inside, 2: leaving
};

/// When a unit last used a targeted power (the power may need a moment before it shows as used).
struct AIAbilityUse
{
	ObjectID			m_unit;
	UnsignedInt		m_frame;
};

/// An infantryman on his way into (or inside) a defensive structure of ours as its standing garrison.
struct AIBunkerRecord
{
	ObjectID			m_unit;
	ObjectID			m_building;
	UnsignedInt		m_since;
	Int						m_phase;						///< 0: entering, 1: inside
	Int						m_class;						///< 1: anti-armour, 0: anti-infantry
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
	Real counterWeights( Real *weight, const AICombatFigures **enemyFig ) const;
	Real teamCounterScore( const TeamPrototype *proto ) const;
	/// Selection weight of a team: its production priority step below the best (hiPri) and how well it counters the enemy.
	Real teamWeight( const TeamPrototype *proto, Int hiPri ) const;
	/// A team that cannot be afforded yet is much better than what could be: wait for it?  False when the money is too far off.
	Bool shouldSaveFor( const TeamPrototype *proto, Real cost );
	void noteTeamPicked() { m_savingSince = 0; }
	/// True when production buildings idle while money piles up (shorten the team timer then).
	Bool productionIsStarved() const { return m_productionStarved; }
	void onUnitProduced( Object *factory, Object *unit );	///< a unit came out of a factory (surplus production takes its own)
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
	/// Distance the army keeps between units because the enemy has area weapons (0: none seen).
	Real splashSpacing() const { return m_spacing; }
	/// Unit level tactics, spread over the frames: kiting, spreading out.
	void updateTactics();

	// ---- batch 2 (AIRaid.cpp, ...) -------------------------------------------------------------
	/// Is the unit on a task of its own (a raid ...) that the team logic must leave alone?
	Bool isDetached( ObjectID id ) const;
	/// One of the player's objects took damage (called by the body module; feeds the protect relation).
	/// The objects of an ally are reported too (allied base support, AIAllyHelp.cpp).
	void onObjectDamaged( Object *victim, ObjectID attacker, Real amount );
	void geoReset();
	/// Is the unit in (or on its way into) a structure of ours as a garrison, or sent to bring down a garrisoned enemy structure?
	Bool isGarrisoned( ObjectID id ) const;
	/// Reads the test bench variant (the feature switches); the build list code needs them before newMap().
	void applyVariant();
	/// Turn of the base layout for a start position that faces the enemy ("layout"); false: the original turn.
	Bool layoutAngle( const Coord3D &start, Real *angle ) const;
	/// A site for a defence structure at the ways into the base ("geo"); false: keep the original place.
	Bool geoDefenceSite( const ThingTemplate *tmpl, Coord3D *pos, Real *angle );
	/// Moves the defence structures of the build list that are not built yet to the ways in.
	void geoPlaceBuildList();
	/// Where the army waits: on the main way in.
	Bool geoRally( Coord3D *pos ) const;
	/// Is the base under attack (an armed enemy force seen in it, or damage to our objects there)?  Base defence has priority over every hold.
	Bool baseUnderThreat() const { return m_bdActive; }

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
	Real forecastAdvantage( const Coord3D *where, Real radius, Real *ourPower, Real *theirPower ) const;
	Bool checkWaveLaunch( const Coord3D *objective );
	void checkWaveOnTheWay( const Coord3D *waveCenter );
	void reinforceWave();
	Bool mergeOn() const;
	Bool hasFreeMember( Team *team ) const;
	void removeDetachedFrom( AIGroupPtr group, Team *team ) const;
	void orderTeamMove( Team *team, const Coord3D *pos );
	void orderTeamAttackMove( Team *team, const Coord3D *pos );
	void sendReinforcementsToThreat();
	void updateScout();
	void updateSiege();
	void updateArmy();
	Bool rallyPoint( Coord3D *pos );
	Real waveTarget( Real *stallNeed = nullptr ) const;
	Real alliedValueNear( const Coord3D *center, Team *except ) const;

	// tactics (AITactics.cpp)
	enum { MAX_STEPS = 16 };
	AIStepRecord *findStep( ObjectID unit );
	void dropStep( AIStepRecord *rec );
	void updateSteps();
	void refreshSplashThreat();
	Bool planSpread( Object *unit, Object *victim, Coord3D *to, UnsignedInt *until );
	void unitTactics( Object *unit, AITeamRecord *team );
	Bool planKite( Object *unit, Object *victim, const AITeamRecord *team, Coord3D *to, UnsignedInt *until );
	Bool enemyCanSee( const Object *victim ) const;
	Bool kitingOn() const;

	// economic raids (AIRaid.cpp)
	enum { MAX_RAIDERS = 4 };
	Bool raidOn() const;
	void updateRaid();
	void tryStartRaid();
	void updateRaidOut();
	void sendRaidersHome( const char *why );
	void endRaid();
	Bool raidTargetSafe( const Coord3D &pos, Real partyValue, Real *guard, const Coord3D *from ) const;
	Bool findRaidTarget( const Coord3D &from, const Object *raider, Real partyValue, Real radius, Bool liveObjects, ObjectID except, ObjectID *id, Coord3D *pos ) const;
	Int collectRaiders( Object **out, Int maxCount, const Object *forTarget );
	void orderRaiders( Object *target, const Coord3D *aim );
	void dropDeadRaiders();

	// avoid static defences (AIRoute.cpp)
	enum { MAX_BREACHERS = 6, MAX_BREACH_TARGETS = 3 };
	Bool routeOn() const;
	Bool routeActive() const;
	Int collectDefenceCircles( AIRouteCircle *circles, Int maxCircles, ObjectID *ids ) const;
	void planWaveRoute( const Coord3D &from, const Coord3D &objective );
	void advanceWaveRoute( const Coord3D &waveCenter );
	Bool routeHolds( const AITeamRecord *rec, const Coord3D &teamCenter ) const;
	Bool isBreacher( ObjectID id ) const;
	void startBreach( const Coord3D &from, const Coord3D &objective, const AIRouteCircle *circles, const ObjectID *ids, Int numCircles );
	void orderBreacher( ObjectID id );
	void updateBreach();

	// repair and heal (AIRepair.cpp)
	enum { MAX_PATIENTS = 6, MAX_SITES = 8 };
	Bool repairOn() const;
	Bool isPatient( ObjectID id ) const;
	void refreshRepairSites();
	Object *findRepairSite( Object *unit, Int kind ) const;
	void sendPatientBack( Object *unit );
	void dropPatient( Int index );
	void updatePatients();
	void tryStartRepairs();
	void updateRepair();

	// standing garrisons (AIBunker.cpp)
	enum { MAX_BUNKER_MEN = 24 };
	Bool bunkerOn() const;
	Bool isGarrisonPost( Object *obj ) const;
	Bool isBunkerMan( ObjectID id ) const;
	const AICombatFigures *seenEnemyFigures( Bool infantry ) const;
	Int bunkerClass( const AICombatFigures *f ) const;
	Real bunkerArmourShare() const;
	Real postThreatScore( Object *post );
	void trainForBunker( Int wantedClass );
	void updateBunkers();

	// surplus production (AISurplus.cpp)
	enum { MAX_SURPLUS_ORDERS = 8, MAX_SURPLUS_UNITS = 32 };
	Bool surplusOn() const;
	Real unitCounterScore( const AICombatFigures *f ) const;
	void updateSurplus();
	void adoptSurplusUnits();

	// razing (AIStrategy.cpp)
	Bool razeStructures( Team *team, const Coord3D &center );
	void updateContest();

	// airborne insertion (AIAirborne.cpp)
	enum { MAX_SQUAD = 8 };
	Bool airborneOn() const;
	Bool isAirborne( ObjectID id ) const;
	Int collectAntiAirCircles( AIRouteCircle *circles, Int maxCircles ) const;
	Bool dropZoneSafe( const Coord3D &p, Real squadValue ) const;
	void endAirMission( const char *why );
	void tryStartAirMission();
	void updateAirMission();
	void updateAirborne();

	// unit abilities (AIAbility.cpp)
	enum { ABILITY_MEMORY = 12 };
	Bool abilityOn() const;
	Bool abilityRecentlyUsed( ObjectID unit ) const;
	void noteAbilityUsed( ObjectID unit );
	Object *pickAbilityTarget( Object *unit, const CommandButton *button, Bool wantEnemy, Bool wantAlly, Real *score ) const;
	Bool pickAbilityPlace( Object *unit, const CommandButton *button, Coord3D *where, Real *score ) const;
	void useAbilities( Object *unit );
	void updateAbilities();

	// garrisons (AIGarrison.cpp)
	enum { MAX_GARRISONED = 12, MAX_CLEANERS = 4 };
	Bool garrisonOn() const;
	Bool baseUnderAttack( Coord3D *where, Real *value, Real *range ) const;
	void dropGarrisoned( Int index );
	void updateGarrisonDefence();
	void updateGarrisonClearing();
	void updateGarrison();

	// terrain-aware defence (AIGeo.cpp)
	Bool geoOn() const;
	Bool layoutOn() const;
	Bool geoPrepare();
	Bool geoCompute();
	Int geoEnemyStarts( Coord3D *out, Int maxStarts, const Coord3D *notNear ) const;
	void geoFindChoke( const GeoPoly &poly, Int crossIdx, AIEntrance &e, Real maxFromCross ) const;
	void updateGeo();

	// base defence priority (AIBaseDefence.cpp)
	Bool baseDefenceOn() const;
	Bool baseAlarmStale( UnsignedInt now ) const;
	Bool baseAlarmBlocksWaves( UnsignedInt now ) const;
	Bool teamWavesOn() const;
	void noteBaseDamage( Object *victim, ObjectID attacker, Real amount );
	Bool findBaseThreat( Coord3D *where, Real *value ) const;
	void sendTeamToBase( Team *team, AITeamRecord &rec, const Coord3D &where );
	void updateBaseDefence();

	// allied base support (AIAllyHelp.cpp)
	enum { MAX_HELP_TEAMS = 8 };
	Bool allyHelpOn() const;
	Bool isAllyHelper( TeamID id ) const;
	void refreshAllyBases();
	void onAllyObjectDamaged( Object *victim, ObjectID attacker, Real amount );
	Bool findAllyThreat( Int idx, Coord3D *where, Real *value, Bool *hit ) const;
	Real allyDefenceNear( Int idx, const Coord3D &where, Real *ours ) const;
	Real ownKnownThreat( const Coord3D &allyCenter, Real allyZone ) const;
	void pruneAllyHelpers();
	UnsignedInt allyHelpFrames( UnsignedInt now ) const;
	Int releaseAllyHelpers();
	void endAllyHelp( const char *why );
	void updateAllyHelp();

	// defend the workers (AITactics.cpp, AIProtect.cpp)
	Bool protectOn() const;
	void updateProtection();

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
	AITactics::SplitLedger	m_ledger;		///< split fire: damage assigned to targets, see assignDamage
	AIStepRecord	m_steps[MAX_STEPS];				///< units that are kiting
	Int						m_numSteps;
	Int						m_tacticTeam;							///< round robin over the units of the field teams
	Int						m_tacticUnit;
	Int						m_kiteStarts;							///< statistics for the trace
	Int						m_kiteResumes;
	Int						m_kiteRejectFast;
	Int						m_kiteRejectCorner;
	Real					m_spacing;								///< spread out: distance kept between units against area weapons (0: not needed)
	UnsignedInt		m_spacingUntil;
	Int						m_spreadMoves;						///< statistics for the trace
	Int						m_spreadSteps;
	Int						m_mergedTeams;						///< statistics for the trace: new teams kept at the rally point during a wave, reinforcements sent there
	Int						m_mergedUnits;
	Int						m_followUps;
	UnsignedInt		m_launchBlockedSince;			///< fight check: frame since which the launch of the wave is held back (0 = not)
	UnsignedInt		m_nextLaunchCheck;
	UnsignedInt		m_waveBadSince;						///< frame since which the wave on its way looks too weak for the fight ahead
	UnsignedInt		m_nextWaveCheck;
	Int						m_launchesHeld;						///< statistics for the trace
	Int						m_launchesForced;
	Int						m_pullbacks;

	Int						m_splitPicks;			///< statistics for the trace: target picks, and picks changed by split fire, threat rules ...
	Int						m_splitSwitches;
	Int						m_threatSwitches;
	Int						m_supportPicks;
	Int						m_longRangePicks;
	ObjectID			m_raiders[MAX_RAIDERS];		///< economic raids: the party that is out (or on its way home)
	Int						m_numRaiders;
	ObjectID			m_raidTarget;
	Coord3D				m_raidAim;
	Int						m_raidPhase;							///< 0: none, 1: out, 2: going home
	UnsignedInt		m_raidStart;
	UnsignedInt		m_raidPhaseFrame;
	UnsignedInt		m_raidBadSince;
	UnsignedInt		m_raidCooldown;
	UnsignedInt		m_nextRaidCheck;
	UnsignedInt		m_raidNoteFrame;
	Real					m_raidPartyValue;
	Int						m_raidsLaunched;					///< statistics for the trace
	Int						m_raidKills;
	Int						m_raidPullbacks;
	Int						m_raidLosses;
	Coord3D				m_route[AIROUTE_MAX_POINTS];		///< the waypoints of the wave that is out (then its objective)
	Int						m_routeLen;
	Int						m_routeIdx;
	UnsignedInt		m_routeLegStart;
	UnsignedInt		m_routeArrived;
	ObjectID			m_breachers[MAX_BREACHERS];			///< units that out-range the defences that cover the objective, sent ahead
	Int						m_numBreachers;
	ObjectID			m_breachTargets[MAX_BREACH_TARGETS];
	Int						m_numBreachTargets;
	Coord3D				m_breachStage;
	UnsignedInt		m_breachStart;
	UnsignedInt		m_nextBreachCheck;
	Bool					m_breachHold;										///< the wave waits at the staging point for the breachers
	Bool					m_breachFallback;								///< the breachers have fallen back to the wave because enemy troops came for them
	Int						m_routesPlanned;								///< statistics for the trace
	Int						m_breachesStarted;
	Int						m_breachKills;
	AIPatient			m_patients[MAX_PATIENTS];		///< repair and heal trips
	Int						m_numPatients;
	ObjectID			m_sites[MAX_SITES];					///< repair and heal pads we own
	Int						m_numSites;
	UnsignedInt		m_nextSiteScan;
	UnsignedInt		m_nextRepair;
	Int						m_repairTrips;							///< statistics for the trace
	Int						m_repairsDone;
	Int						m_dozerRepairs;
	Bool					m_bdActive;											///< base defence: a threat is in the base zone
	UnsignedInt		m_bdLastThreat;
	UnsignedInt		m_bdSince;
	UnsignedInt		m_bdDamageFrame;								///< latest damage to one of our objects inside the zone by an enemy
	UnsignedInt		m_nextBaseDefence;
	UnsignedInt		m_bdMuteUntil;									///< a stale alarm is not raised again by the same force until this frame
	Real					m_bdMuteValue;
	UnsignedInt		m_bdWeakFrame;									///< last trace line about a threat that the teams at home do not go out to
	AIBunkerRecord m_bunkerMen[MAX_BUNKER_MEN];			///< infantry that hold defensive structures for good
	Int						m_numBunkerMen;
	UnsignedInt		m_nextBunker;
	UnsignedInt		m_nextBunkerTrain;
	UnsignedInt		m_bunkerQueueFrame;
	Int						m_bunkerQueued;									///< infantry ordered for the posts and not seen yet
	Int						m_bunkerPosts;									///< statistics for the trace
	Int						m_bunkerEntered;
	Int						m_bunkerTrained;
	UnsignedInt		m_nextSurplus;
	ObjectID			m_surplusFactory[MAX_SURPLUS_ORDERS];	///< surplus units ordered and not out yet: the factory,
	Int						m_surplusThing[MAX_SURPLUS_ORDERS];		///< ... the template id of the unit,
	UnsignedInt		m_surplusOrderFrame[MAX_SURPLUS_ORDERS];///< ... and when it was ordered
	Int						m_numSurplusOrders;
	ObjectID			m_surplusUnits[MAX_SURPLUS_UNITS];		///< surplus units out of the factory that wait for a team
	Int						m_numSurplusUnits;
	Int						m_surplusOrdered;										///< statistics for the trace
	Int						m_surplusInfantry;
	Real					m_surplusSpent;
	Int						m_surplusAdopted;
	Int						m_razeOrders;										///< statistics for the trace
	Int						m_huntOrders;
	Int						m_contestOrders;
	UnsignedInt		m_nextContest;
	Int						m_airPhase;											///< airborne insertion: 0 = no mission
	ObjectID			m_airTransport;
	ObjectID			m_squad[MAX_SQUAD];
	Int						m_numSquad;
	Coord3D				m_airPath[AIROUTE_MAX_POINTS];	///< waypoints out, the last is the drop point
	Int						m_airPathLen;
	Int						m_airPathIdx;
	Coord3D				m_airBack[AIROUTE_MAX_POINTS];	///< waypoints back, the last is the rally point
	Int						m_airBackLen;
	Coord3D				m_airTarget;
	ObjectID			m_airTargetID;
	UnsignedInt		m_airPhaseFrame;
	UnsignedInt		m_airStart;
	UnsignedInt		m_airAssaultStart;
	UnsignedInt		m_airCooldown;
	UnsignedInt		m_nextAirCheck;
	UnsignedInt		m_airNoteFrame;
	Int						m_airMissions;									///< statistics for the trace
	Int						m_airDrops;
	Int						m_airKills;
	Int						m_airLost;
	Int						m_airNoPath;
	AIAbilityUse	m_abilityUsed[ABILITY_MEMORY];
	Int						m_abilityTeam;									///< round robin over the units of the field teams
	Int						m_abilityUnit;
	UnsignedInt		m_nextAbility;
	Int						m_abilityUses;									///< statistics for the trace
	AIGarrisonRecord m_garrisoned[MAX_GARRISONED];	///< infantry that hold structures of ours against an attack
	Int						m_numGarrisoned;
	UnsignedInt		m_garrisonLastThreat;
	UnsignedInt		m_nextGarrison;
	ObjectID			m_cleaners[MAX_CLEANERS];				///< units sent against an enemy structure that holds a garrison
	Int						m_numCleaners;
	ObjectID			m_clearTarget;
	UnsignedInt		m_clearStart;
	UnsignedInt		m_nextClearSearch;
	Int						m_garrisonEntered;							///< statistics for the trace
	Int						m_garrisonSeconds;							///< unit-seconds spent inside, and of those the seconds in which the unit fired
	Int						m_garrisonFiring;
	Int						m_garrisonClearJobs;
	Int						m_garrisonClears;
	enum { GEO_MAX_ENTRANCES = 4, GEO_MAX_SITES = 16 };
	Int						m_numEntrances;									///< terrain-aware defence: the ways into the base
	AIEntrance		m_entrances[GEO_MAX_ENTRANCES];
	Bool					m_geoReady;
	Bool					m_geoWall;											///< terrain closes the perimeter into gaps
	Int						m_geoTries;
	Int						m_geoMain;											///< index of the way in with the largest expected threat
	UnsignedInt		m_nextGeo;
	UnsignedInt		m_nextGeoWeights;
	UnsignedInt		m_geoMoved;											///< build list entries (by position in the list) that were looked at
	Real					m_geoRing;
	Int						m_numGeoSites;
	Coord3D				m_geoSites[GEO_MAX_SITES];
	Int						m_geoPlaced;										///< statistics for the trace
	Int						m_geoPlacedFixed;
	Int						m_geoRallyMoves;
	Coord3D				m_bdPos;
	Coord3D				m_bdDamagePos;
	Real					m_bdValue;
	Int						m_bdAlarms;											///< statistics for the trace
	Int						m_bdOrders;
	Int						m_bdIdleAtAlarm;
	Coord3D				m_allyBase[MAX_PLAYER_COUNT];		///< allied base support: the base of each ally (by player index), from its structures
	Real					m_allyRadius[MAX_PLAYER_COUNT];	///< ... its radius (0: not an ally, or no base)
	UnsignedInt		m_nextAllyBases;
	UnsignedInt		m_allyDamageFrame[MAX_PLAYER_COUNT];	///< latest damage to an object of the ally inside its base zone by an enemy
	Coord3D				m_allyDamagePos[MAX_PLAYER_COUNT];
	Bool					m_ahActive;											///< an allied base is under attack (the alarm, whether help was sent or not)
	Int						m_ahPlayer;											///< the ally whose base it is (player index, -1: none)
	Coord3D				m_ahPos;
	Real					m_ahValue;
	Real					m_ahPeak;												///< the largest threat seen in the ally's base in the last 20 s: what the help is sized against
	UnsignedInt		m_ahPeakFrame;
	UnsignedInt		m_ahSince;
	UnsignedInt		m_ahLastThreat;
	UnsignedInt		m_ahHelpSince;									///< since when teams of this alarm are out (0: none now)
	UnsignedInt		m_ahAlarmHelped;								///< frames of this alarm with teams out, before m_ahHelpSince
	UnsignedInt		m_ahLastSend;
	UnsignedInt		m_nextAllyHelp;
	UnsignedInt		m_ahMuteUntil;									///< a stale alarm is not raised again by the same force until this frame
	Int						m_ahMutePlayer;
	Real					m_ahMuteValue;
	TeamID				m_ahTeams[MAX_HELP_TEAMS];			///< the teams that were sent to the ally
	Int						m_ahNumTeams;
	UnsignedInt		m_ahNoteFrame;									///< last trace line about help that was not needed or not possible
	Int						m_ahAlarms;											///< statistics for the trace
	Int						m_ahOrders;
	Int						m_ahTeamsSent;
	UnsignedInt		m_ahHelpFrames;
	AIProtect			m_protect;							///< protect relation: armed units that answer an attack on the gatherers and workers
	UnsignedInt		m_nextProtect;
	Bool					m_protectActive;
	Bool					m_trace;					///< print decisions (test bench: variant "trace")
	UnsignedInt		m_nextStatus;
};
