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

// AITacticsCore.h
// Unit level tactics that the Expert computer player and the stances of the player assists share: when a step
// back is worth it (kiting), where to step to keep apart from friends (spreading), what to shoot at (target
// scoring and the split fire ledger) and where the army gathers (rally point).  The functions are pure
// functions of synchronised game state (positions, weapons, health, the tables of AICombatModel): they hold no
// memory of their own except what the caller passes in, never use the wall clock and never iterate containers
// keyed by pointers.  The AI (AIStrategy, AI.cpp) and the stances (PlayerAssist) call them with their own
// settings and their own view of the enemy.

#pragma once

#include "Common/GameCommon.h"

class Object;
class Player;
struct AICombatFigures;
struct AISkillSettings;

namespace AITactics
{

//-------------------------------------------------------------------------------------------------
/// The numbers the tactics need (the AI takes them from its skill settings, the stances from the same block).
struct Params
{
	Real	m_kiteMinReloadSeconds;		///< shortest wait between two volleys that is worth a step back
	Real	m_kiteRangeFactor;
	Real	m_kiteSpeedFactor;
	Real	m_kiteGroupRadius;				///< a unit stays within this distance of its group
	Real	m_kiteMinThreat;
	Real	m_splashRadiusThreshold;	///< enemy weapons with a blast at least this big make units keep apart
	Real	m_maxSpacing;
	Real	m_splitWindowSeconds;
};
Params paramsOf( const AISkillSettings &skill );

//-------------------------------------------------------------------------------------------------
/// An enemy the tactics look at.
struct Contact
{
	ObjectID								m_id;
	Coord3D									m_pos;
	const AICombatFigures		*m_fig;
	UnsignedInt							m_age;			///< frames since it was last seen (0: in sight now)
};

/// The enemies a caller knows, in whatever form it keeps them.
class ContactList
{
public:
	virtual ~ContactList() { }
	virtual Int count() const = 0;
	virtual Bool get( Int index, Contact *out ) const = 0;
};

//-------------------------------------------------------------------------------------------------
// kiting
enum KiteResult
{
	KITE_NONE = 0,					///< no step back
	KITE_REJECT_FAST,				///< a faster enemy is about
	KITE_REJECT_CORNER,			///< no room behind the unit
	KITE_YES								///< step: 'to' and 'until' are set
};

/// Is a step back worth it right now, and where to?  'unit' attacks 'victim'; 'home' (may be null) is the place of its group.
KiteResult planKite( const Params &params, Object *unit, Object *victim, const Coord3D *home, const ContactList &contacts, Coord3D *to, UnsignedInt *until );

//-------------------------------------------------------------------------------------------------
// spreading out
/// Distance to keep between units against a blast of this radius (0: none needed).
Real spacingForBlast( const Params &params, Real blast );

/**
 * A spot to move to because 'unit' stands too close to its friends, or false.  With a victim, the unit is between
 * two shots: the spot must stay in range of the victim.
 */
Bool planSpread( const Params &params, Real spacing, Object *unit, Object *victim, const Coord3D *home, Coord3D *to, UnsignedInt *until );

//-------------------------------------------------------------------------------------------------
// target selection and split fire
/// The units around the shooter that a target is dangerous to: its cost-weighted kill rate is the threat.
struct Mates
{
	enum { MAX = 8 };
	const AICombatFigures	*m_fig[MAX];
	Int										m_num;
	Real									m_cost;
	Mates() : m_num( 0 ), m_cost( 0.0f ) { }
	void add( const AICombatFigures *f );
};

struct TargetScore
{
	Real	m_original;			///< the original rule of the game's AI (it can hurt us: first)
	Real	m_threat;				///< with the threat rules: the share of the value around that it destroys, healers, what outranges
	Int		m_kind;					///< 1: picked as support unit, 2: picked as long range unit, 0: other
};

/// Scores a candidate for the shooter 'mine' that has 'range' as its search range.  'health' is 0..1, 'dist' is the distance to the candidate.
void scoreTarget( const AICombatFigures *mine, const AICombatFigures *f, Real health, Real dist, Real range, Bool threatFirst, const Mates &mates, TargetScore *out );

/// What the split fire does to the score of a target: damage already on its way counts against it.
Real splitPenalty( Real assigned, Real health );

/// Damage that units have just assigned to a target: a small table with expiry.
class SplitLedger
{
public:
	enum { SIZE = 32 };
	struct Entry
	{
		ObjectID			m_target;
		Real					m_damage;
		UnsignedInt		m_expire;
	};
	SplitLedger() { clear(); }
	void clear();
	Real assigned( ObjectID target, UnsignedInt now ) const;
	void assign( ObjectID target, Real damage, UnsignedInt now, UnsignedInt windowFrames );
	/// Takes back damage that a unit assigned and will not deal after all (it took another target, or renews its share).
	void release( ObjectID target, Real damage, UnsignedInt now );
	const Entry &entry( Int i ) const { return m_entries[i]; }
	Entry &entry( Int i ) { return m_entries[i]; }
private:
	Entry	m_entries[SIZE];
};

//-------------------------------------------------------------------------------------------------
// the rally point
/// Where a player starts on the map (the waypoint of its start position, known to everybody in a skirmish).
Bool playerStart( Player *player, Coord3D *out );

/// In front of a base, on the side of 'toward': where the army gathers and where a unit that pulls out goes.
void rallyAhead( const Coord3D &base, Real baseRadius, const Coord3D &toward, Coord3D *out );

}	// namespace AITactics
