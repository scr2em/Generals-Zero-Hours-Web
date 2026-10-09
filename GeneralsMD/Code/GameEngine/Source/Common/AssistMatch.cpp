/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: AssistMatch.cpp //////////////////////////////////////////////////////
//
// The player assist test bench, see AssistMatch.h for the command line.
//
// The match is set up and driven like an -aiMatch (AIMatch.cpp): a skirmish setup made from the command line, a
// MSG_NEW_GAME straight into TheCommandList, then TheGameLogic->UPDATE() in a loop, no rendering, no frame pacing.
// The difference: slot 0 is the local human player, the match allows the player assists, and a script drives it.
//
// The orders of the script are the game messages the user interface sends, appended to TheCommandList for the
// local player: they reach GameLogic::logicMessageDispatcher() exactly as clicks and hotkeys would (selection
// included), and from there the player assists (PlayerAssist::onMessage, AIGroup::groupMoveToPosition). Nothing in
// the assists is called directly. Enemies are driven with direct AI orders, the way a script of a mission does.
//
// The script: steps separated by ';' or new lines, '#' starts a comment. A step is a verb, positional arguments and
// key=value options. Steps run one after the other in the same logic frame until a step waits.
//
//   Units and names
//     spawn <name> <entry>[+<entry>...] [at=<pos>]   create units with -assistTest entries (player:template:count[:ref:dx:dy],
//                                                  see AssistTest.cpp); with at= the first unit stands at <pos>
//     name <name> <objects>                        give a set of objects a name
//     snapshot <objects> [as=<name>]               remember where the units stand relative to their centre (for sameshape)
//     dump <objects>                               print the units' state (ASSISTMATCH_DUMP lines)
//   Orders of the human player (game messages, as the user interface sends them)
//     select <objects>                             MSG_CREATE_SELECTED_GROUP (a new selection)
//     group <n> / selectgroup <n>                  MSG_CREATE_TEAMn from the selection / MSG_SELECT_TEAMn (hotkey groups)
//     formation <type>                             MSG_ASSIST_FORMATION (type: none line column wedge box loose keep)
//     fmove <posA> [<posB>] [type=<t>] [attack=1]  MSG_ASSIST_FORMATION_MOVE: a right-drag from A to B (no B: A), type by default
//                                                  the formation the selection shares (as the UI does)
//     move|attackmove|guard <pos>                  MSG_DO_MOVETO / MSG_DO_ATTACKMOVETO / MSG_DO_GUARD_POSITION (a right click)
//     attack <objects>                             MSG_DO_ATTACK_OBJECT (the first of them)
//     stop                                         MSG_DO_STOP
//     protect <protectors> <protected>|group=<n>   MSG_ASSIST_PROTECT (hotkey group or -1, protectors, protected)
//     unprotect                                    MSG_ASSIST_UNPROTECT for the selection
//     send <command> [int:<n>|bool:<0|1>|real:<x>|pos:<pos>|obj:<objects>]...
//                                                  any other command of the player by name (MSG_ASSIST_STANCE or ASSIST_STANCE),
//                                                  with its arguments in order (obj: appends every object), e.g. the orders of
//                                                  assists that have no verb here
//   Anything else (any player's units, directly)
//     ai <objects> attack <objects>|move <pos>|attackmove <pos>|guard <pos>|stop
//     damage <objects> <amount>[%] [by=<objects>]  damage (or a share of the full health) as if the first of by= had hit them
//                                                  (that is what raises a protect alarm); armour does not count
//     kill <objects>
//   Time and checks
//     wait <frames>
//     until [not] <condition> [max=<frames>]       wait until the condition holds (a check: fails after max=, default 900)
//     expect [not] <condition>                     check now
//
// Objects: <name>, <name>[i] (the i-th, from 0), cc:<slot> (the command center of a player), sel (the selection),
// joined with '+'. Positions: x,y (world), <objects> (their centre), map (the centre of the map), followed by an offset:
// +dx,dy or -dx,dy (world), or ^f,l (f towards the centre of the map, l to the left of that).
//
// Conditions (on every unit; any=1: on one of them):
//   formation <objects> <type>          the units have this formation (PlayerAssist::formationOf)
//   shape <objects> <type> [tol=30] [spacing=<s>]   the slots the units were given (their formation offsets) are the slots of
//                                       a formation of that type (PlayerAssist::layoutSlots) facing the way their last move order
//                                       says (the direction of the move, or away from the group across a dragged front line), one
//                                       unit each, and every unit stands within tol of its slot at the destination of that order.
//                                       The spacing is the slot grid's (the closest two slots) unless given
//   sameshape <objects> [tol=30] [as=<snapshot>]    the units stand as they did at the snapshot, relative to their centre
//   ahead <objectsA> <objectsB> [by=1]  A stands further forward (the heading of A's last move order) than B, on average
//   nearline <objects> <posA> <posB> [tol=30]       the units stand on the segment from A to B
//   near <objects> <pos> [tol=50] [centre=1]        the units (or their centre) are within tol of the position
//   linked / unlinked <objects>          the units are / are not protectors (protect links)
//   protects <objects> <protected>       their links cover these objects
//   state <objects> home|responding|returning       the state of their protect links
//   athome <objects> [tol=45]            their links are at home and they stand within tol of the home
//   homeat <objects> <pos> [tol=60]      the centre of their homes is within tol of the position
//   atsnapshot <objects> [tol=30] [as=<snapshot>]   the units stand where they stood at the snapshot
//   apart <objects> min=<d>              no two of the units are closer than d
//   health <objects> above|below <percent>
//   alive / dead / damaged / idle <objects>
//
// Every check is printed as "ASSISTMATCH_CHECK PASS|FAIL ..." and the end result as "ASSISTMATCH_RESULT {json}".
//
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistMatch.h"

#include "Common/AIMatchShared.h"
#include "Common/AssistOptions.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/Pathfinder/PathfindConstants.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/Protect.h"
#include "GameLogic/Squad.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Weapon.h"
#include "GameNetwork/GameInfo.h"

#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace AIMatchShared;

namespace
{

const char *const kSchema = "zh-assistbench-1";
const UnsignedInt kMaxUpdatesToStart = 100;
const UnsignedInt kMaxUpdatesWithoutProgress = 20000;
const UnsignedInt kDefaultMaxFrames = 30 * 60 * LOGICFRAMES_PER_SECOND;	// game time, a safety net
const UnsignedInt kDefaultUntilFrames = 900;

const char *const kFormationNames[AFORM_COUNT] = { "none", "line", "column", "wedge", "box", "loose", "keep" };
const char *const kStateNames[] = { "home", "responding", "returning" };

Int formationByName(const std::string &name)
{
	for (Int i = 0; i < AFORM_COUNT; ++i)
		if (lowered(name) == kFormationNames[i])
			return i;
	return -1;
}

Bool parseReal(const std::string &text, Real &out)
{
	if (text.empty())
		return FALSE;
	char *end = nullptr;
	const double v = strtod(text.c_str(), &end);
	if (end == text.c_str() || *end != 0)
		return FALSE;
	out = (Real)v;
	return TRUE;
}

Real dist2D(const Coord3D &a, const Coord3D &b)
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return sqrtf(dx * dx + dy * dy);
}

// ------------------------------------------------------------------------------------------------
// Configuration, from the command line
// ------------------------------------------------------------------------------------------------
enum SlotKind { KIND_HUMAN, KIND_IDLE, KIND_AI };

struct PlayerSpec
{
	PlayerSpec() : kind(KIND_AI), state(SLOT_EASY_AI), team(-1), startPos(-1), templateIndex(PLAYERTEMPLATE_RANDOM) {}
	SlotKind kind;
	SlotState state;
	std::string kindName;
	std::string side;
	Int team;
	Int startPos;
	Int templateIndex;
};

struct Config
{
	Config() : seed(0), seedGiven(FALSE), maxFrames(kDefaultMaxFrames), assists(TRUE), startingCash(-1), debug(TRUE) {}
	std::string map;
	std::string mapPath;
	std::string steps;
	std::string label;
	std::vector<PlayerSpec> players;
	Int seed;
	Bool seedGiven;
	UnsignedInt maxFrames;
	Bool assists;
	Int startingCash;
	Bool debug;
};

Bool parsePlayers(const std::string &text, std::vector<PlayerSpec> &out, std::string &error)
{
	std::vector<std::string> entries = split(text, ',');
	for (size_t e = 0; e < entries.size(); ++e)
	{
		std::vector<std::string> f = split(trimmed(entries[e]), ':');
		if (f.size() < 2 || f.size() > 4)
		{
			error = "player '" + entries[e] + "' is not kind:side[:team[:start]]";
			return FALSE;
		}
		PlayerSpec spec;
		spec.kindName = lowered(trimmed(f[0]));
		if (spec.kindName == "human")
			spec.kind = KIND_HUMAN, spec.state = SLOT_PLAYER;
		else if (spec.kindName == "idle")
			spec.kind = KIND_IDLE, spec.state = SLOT_EASY_AI;	// a computer player whose AI is removed when the match starts
		else if (spec.kindName == "easy")
			spec.state = SLOT_EASY_AI;
		else if (spec.kindName == "normal" || spec.kindName == "medium")
			spec.state = SLOT_MED_AI, spec.kindName = "normal";
		else if (spec.kindName == "hard" || spec.kindName == "brutal")
			spec.state = SLOT_BRUTAL_AI, spec.kindName = "hard";
		else if (spec.kindName == "expert")
			spec.state = SLOT_EXPERT_AI;
		else
		{
			error = "unknown player kind '" + f[0] + "' (human, idle, easy, normal, hard or expert)";
			return FALSE;
		}
		spec.side = trimmed(f[1]);
		if (f.size() > 2 && !trimmed(f[2]).empty() && (!parseSigned(trimmed(f[2]), spec.team) || spec.team < -1))
		{
			error = "bad team '" + f[2] + "'";
			return FALSE;
		}
		if (f.size() > 3 && !trimmed(f[3]).empty())
		{
			Int start = 0;
			if (!parseSigned(trimmed(f[3]), start) || start < 0)
			{
				error = "bad start position '" + f[3] + "' (1 based, or 0 for random)";
				return FALSE;
			}
			spec.startPos = start - 1;
		}
		out.push_back(spec);
	}
	return TRUE;
}

Bool parseConfig(Config &cfg, std::string &error)
{
	Bool playersGiven = FALSE;
	for (int i = 1; i < __argc; ++i)
	{
		const char *arg = __argv[i];
		const char *eq = strchr(arg, '=');
		if (arg[0] == '-' || eq == nullptr)
			continue;
		const std::string key = lowered(std::string(arg, eq - arg));
		const std::string value = eq + 1;
		if (key == "map")
			cfg.map = value;
		else if (key == "players")
		{
			if (!parsePlayers(value, cfg.players, error))
				return FALSE;
			playersGiven = TRUE;
		}
		else if (key == "seed")
		{
			if (!parseSigned(value, cfg.seed))
			{
				error = "bad seed '" + value + "'";
				return FALSE;
			}
			cfg.seedGiven = TRUE;
		}
		else if (key == "steps")
			cfg.steps = value;
		else if (key == "label")
			cfg.label = value;
		else if (key == "maxframes")
		{
			if (!parseUnsigned(value, cfg.maxFrames) || cfg.maxFrames == 0)
			{
				error = "bad maxframes '" + value + "'";
				return FALSE;
			}
		}
		else if (key == "assists" || key == "debug")
		{
			const Bool on = value != "0" && lowered(value) != "no" && lowered(value) != "false";
			(key == "assists" ? cfg.assists : cfg.debug) = on;
		}
		else if (key == "cash")
		{
			if (!parseSigned(value, cfg.startingCash) || cfg.startingCash < 0)
			{
				error = "bad cash '" + value + "'";
				return FALSE;
			}
		}
		else
		{
			error = "unknown option '" + key + "'";
			return FALSE;
		}
	}
	if (cfg.map.empty())
	{
		error = "map=<map> is required";
		return FALSE;
	}
	if (!playersGiven || cfg.players.size() < 2 || cfg.players[0].kind != KIND_HUMAN)
	{
		error = "players=human:<side>,<idle|easy|normal|hard|expert>:<side>,... needs the human player first and at least one more";
		return FALSE;
	}
	for (size_t i = 1; i < cfg.players.size(); ++i)
	{
		if (cfg.players[i].kind == KIND_HUMAN)
		{
			error = "only the first player can be human";
			return FALSE;
		}
	}
	if ((Int)cfg.players.size() > MAX_SLOTS)
	{
		error = format("at most %d players", (int)MAX_SLOTS);
		return FALSE;
	}
	if (!cfg.seedGiven)
	{
		error = "seed=<n> is required";
		return FALSE;
	}
	if (cfg.steps.empty())
	{
		error = "steps=<script> is required";
		return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// The script
// ------------------------------------------------------------------------------------------------
struct Step
{
	std::string text;
	std::vector<std::string> args;				// the verb first, then the positional arguments
	std::map<std::string, std::string> opts;	// key=value
	Bool has(const char *k) const { return opts.find(k) != opts.end(); }
	std::string opt(const char *k, const char *def = "") const
	{
		std::map<std::string, std::string>::const_iterator it = opts.find(k);
		return it == opts.end() ? std::string(def) : it->second;
	}
};

struct VerbInfo
{
	const char *name;
	size_t minArgs;		// positional arguments after the verb
};

const VerbInfo kVerbs[] = {
	{ "spawn", 2 }, { "name", 2 }, { "snapshot", 1 }, { "dump", 1 },
	{ "select", 1 }, { "group", 1 }, { "selectgroup", 1 }, { "formation", 1 }, { "fmove", 1 },
	{ "move", 1 }, { "attackmove", 1 }, { "guard", 1 }, { "attack", 1 }, { "stop", 0 },
	{ "protect", 1 }, { "unprotect", 0 }, { "send", 1 },
	{ "ai", 2 }, { "damage", 2 }, { "kill", 1 },
	{ "wait", 1 }, { "until", 2 }, { "expect", 2 },
};

struct CondInfo
{
	const char *name;
	size_t minArgs;		// after the condition's name
};

const CondInfo kConds[] = {
	{ "formation", 2 }, { "shape", 2 }, { "sameshape", 1 }, { "ahead", 2 }, { "nearline", 3 }, { "near", 2 },
	{ "linked", 1 }, { "unlinked", 1 }, { "protects", 2 }, { "state", 2 }, { "athome", 1 }, { "homeat", 2 },
	{ "alive", 1 }, { "dead", 1 }, { "damaged", 1 }, { "idle", 1 }, { "atsnapshot", 1 }, { "apart", 1 }, { "health", 3 },
};

Bool parseScript(const std::string &text, std::vector<Step> &out, std::string &error)
{
	std::string piece;
	std::vector<std::string> pieces;
	Bool comment = FALSE;
	for (size_t i = 0; i <= text.size(); ++i)
	{
		const char c = i < text.size() ? text[i] : ';';
		if (c == ';' || c == '\n')
		{
			pieces.push_back(piece);
			piece.clear();
			comment = FALSE;
		}
		else if (c == '#')
			comment = TRUE;
		else if (!comment)
			piece += c;
	}
	for (size_t p = 0; p < pieces.size(); ++p)
	{
		Step s;
		std::string word;
		const std::string &t = pieces[p];
		for (size_t i = 0; i <= t.size(); ++i)
		{
			const char c = i < t.size() ? t[i] : ' ';
			if (c == ' ' || c == '\t' || c == '\r')
			{
				if (word.empty())
					continue;
				const size_t eq = word.find('=');
				if (eq != std::string::npos && eq > 0 && !s.args.empty())
					s.opts[lowered(word.substr(0, eq))] = word.substr(eq + 1);
				else
					s.args.push_back(word);
				if (!s.text.empty())
					s.text += ' ';
				s.text += word;
				word.clear();
			}
			else
				word += c;
		}
		if (s.args.empty())
			continue;
		s.args[0] = lowered(s.args[0]);
		const VerbInfo *verb = nullptr;
		for (size_t v = 0; v < ARRAY_SIZE(kVerbs); ++v)
			if (s.args[0] == kVerbs[v].name)
				verb = &kVerbs[v];
		if (verb == nullptr)
		{
			error = "unknown step '" + s.text + "'";
			return FALSE;
		}
		if (s.args.size() < verb->minArgs + 1)
		{
			error = "step '" + s.text + "' needs more arguments";
			return FALSE;
		}
		if (s.args[0] == "until" || s.args[0] == "expect")
		{
			size_t c = 1;
			if (lowered(s.args[c]) == "not")
				++c;
			const CondInfo *cond = nullptr;
			for (size_t k = 0; c < s.args.size() && k < ARRAY_SIZE(kConds); ++k)
				if (lowered(s.args[c]) == kConds[k].name)
					cond = &kConds[k];
			if (cond == nullptr)
			{
				error = "unknown condition in '" + s.text + "'";
				return FALSE;
			}
			if (s.args.size() < c + 1 + cond->minArgs)
			{
				error = "condition '" + s.text + "' needs more arguments";
				return FALSE;
			}
		}
		out.push_back(s);
	}
	if (out.empty())
	{
		error = "the script has no steps";
		return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// The match
// ------------------------------------------------------------------------------------------------
struct Check
{
	Int step;
	UnsignedInt frame;
	std::string kind;		// expect, until or error
	std::string text;
	Bool pass;
	std::string detail;
	std::vector<std::pair<std::string, double> > measured;
};

/// What a move order of the script told the units: where to and facing which way (see PlayerAssist::prepareFormationMove).
struct Order
{
	Order() : frame(0), heading(0.0f), width(0.0f) { dest.zero(); }
	UnsignedInt frame;
	Coord3D dest;
	Real heading;			// radians
	Real width;				// of a dragged front line, 0 for a click
};

/// The outcome of a condition.
struct Eval
{
	Eval() : ok(FALSE), error(FALSE) {}
	Bool ok;
	Bool error;				// the condition could not be evaluated (unknown name, no order ...)
	std::string detail;
	std::vector<std::pair<std::string, double> > measured;
	void num(const char *k, double v) { measured.push_back(std::make_pair(std::string(k), v)); }
	void fail(const std::string &why) { ok = FALSE; error = TRUE; detail = why; }
};

class Runner
{
public:
	Runner(const Config &cfg, const std::vector<Step> &steps)
		: m_cfg(cfg), m_steps(steps), m_human(nullptr), m_updates(0), m_loadMs(0), m_simMs(0), m_startFrame(0),
		  m_stepsDone(0), m_assistsAllowed(FALSE)
	{}

	Bool setup(std::string &error);
	Bool play(std::string &error);
	std::string report(UnsignedInt setupMs, const std::string &abortReason);

private:
	Bool advance(UnsignedInt frames, std::string &error);
	void runStep(Int index, const Step &s, std::string &fatal);
	void addCheck(Int index, const Step &s, const char *kind, Bool pass, const Eval &e);

	Bool resolveObjects(const std::string &ref, std::vector<ObjectID> &ids, std::string &error) const;
	Bool resolveLive(const std::string &ref, std::vector<Object *> &objs, std::string &error) const;
	Bool resolvePos(const std::string &spec, Coord3D &pos, std::string &error) const;
	Coord3D mapCentre() const;
	Bool centreOf(const std::vector<ObjectID> &ids, Coord3D &c) const;

	GameMessage *message(GameMessage::Type t);
	void recordOrder(const std::vector<ObjectID> &ids, const Coord3D &dest, Bool dragged, const Coord3D &a, const Coord3D &b);

	Eval evaluate(const Step &s, size_t first);
	Eval evalShape(const std::vector<ObjectID> &ids, Int type, const Step &s);

	Config m_cfg;
	std::vector<Step> m_steps;
	Player *m_human;
	UnsignedInt m_updates;
	UnsignedInt m_loadMs;
	UnsignedInt m_simMs;
	UnsignedInt m_startFrame;
	Int m_stepsDone;
	Bool m_assistsAllowed;

	std::vector<Check> m_checks;
	std::map<std::string, std::vector<ObjectID> > m_names;
	std::vector<ObjectID> m_selection;
	std::map<Int, std::vector<ObjectID> > m_hotkeys;
	std::map<ObjectID, Order> m_orders;
	std::map<std::string, std::map<ObjectID, Coord2D> > m_snapshots;		// relative to the centre of the units
	std::map<std::string, std::map<ObjectID, Coord3D> > m_places;			// where the units stood
};

// ------------------------------------------------------------------------------------------------
Bool Runner::setup(std::string &error)
{
	if (!findMapPath(m_cfg.map, m_cfg.mapPath, error))
		return FALSE;
	const MapMetaData *md = TheMapCache->findMap(AsciiString(m_cfg.mapPath.c_str()));
	if (md == nullptr || !md->m_isMultiplayer)
	{
		error = "map '" + m_cfg.mapPath + "' is not a skirmish map";
		return FALSE;
	}
	if ((Int)m_cfg.players.size() > md->m_numPlayers)
	{
		error = format("map '%s' has room for %d players, %d requested", m_cfg.mapPath.c_str(), md->m_numPlayers, (int)m_cfg.players.size());
		return FALSE;
	}
	for (size_t i = 0; i < m_cfg.players.size(); ++i)
	{
		if (!findSideTemplate(m_cfg.players[i].side, m_cfg.players[i].templateIndex, error))
			return FALSE;
		if (m_cfg.players[i].startPos >= md->m_numPlayers)
		{
			error = format("start position %d is beyond the %d of the map", m_cfg.players[i].startPos + 1, md->m_numPlayers);
			return FALSE;
		}
	}

	// The skirmish setup the menu would have made: the human player in slot 0 (as SkirmishGameOptionsMenu does it),
	// the computer players after it, the rest closed.
	if (TheSkirmishGameInfo == nullptr)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	else if (TheSkirmishGameInfo->isInGame())
		TheSkirmishGameInfo->endGame();
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	const Int numColors = TheMultiplayerSettings != nullptr ? TheMultiplayerSettings->getNumColors() : 0;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		GameSlot gs;
		if ((size_t)slot < m_cfg.players.size())
		{
			const PlayerSpec &spec = m_cfg.players[slot];
			if (spec.kind == KIND_HUMAN)
			{
				gs.setName(UnicodeString(L"Tester"));
				gs.setState(SLOT_PLAYER, UnicodeString(L"Tester"));
			}
			else
				gs.setState(spec.state);
			gs.setPlayerTemplate(spec.templateIndex);
			gs.setColor(numColors > 0 ? slot % numColors : -1);
			gs.setStartPos(spec.startPos);
			gs.setTeamNumber(spec.team);
		}
		else
			gs.setState(SLOT_CLOSED);
		TheSkirmishGameInfo->setSlot(slot, gs);
	}

	TheSkirmishGameInfo->setMap(AsciiString(m_cfg.mapPath.c_str()));
	TheSkirmishGameInfo->setSeed(m_cfg.seed);
	if (m_cfg.startingCash >= 0)
	{
		Money cash;
		cash.setStartingCash((UnsignedInt)m_cfg.startingCash);
		TheSkirmishGameInfo->setStartingCash(cash);
	}
	TheSkirmishGameInfo->setSuperweaponRestriction(0);
	TheSkirmishGameInfo->setPlayerAssistsAllowed(m_cfg.assists);
	TheSkirmishGameInfo->startGame(0);
	TheWritableGlobalData->m_mapName = AsciiString(m_cfg.mapPath.c_str());
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
Bool Runner::advance(UnsignedInt frames, std::string &error)
{
	for (UnsignedInt i = 0; i < frames; ++i)
	{
		const UnsignedInt before = TheGameLogic->getFrame();
		if (before - m_startFrame >= m_cfg.maxFrames)
		{
			error = format("maxframes=%u reached at step %d", m_cfg.maxFrames, m_stepsDone + 1);
			return FALSE;
		}
		if (!TheGameLogic->isInGame() || TheGameEngine->getQuitting())
		{
			error = format("the game ended by itself at frame %u", before);
			return FALSE;
		}
		UnsignedInt stuck = 0;
		while (TheGameLogic->getFrame() == before)
		{
			TheGameLogic->UPDATE();
			if (++stuck > kMaxUpdatesWithoutProgress)
			{
				error = format("the game logic stopped advancing at frame %u", before);
				return FALSE;
			}
		}
	}
	return TRUE;
}

GameMessage *Runner::message(GameMessage::Type t)
{
	// The message comes from the local player (GameMessage's constructor), like one from the user interface.
	GameMessage *msg = newInstance(GameMessage)(t);
	TheCommandList->appendMessage(msg);
	return msg;
}

Coord3D Runner::mapCentre() const
{
	Region3D r;
	TheTerrainLogic->getExtent(&r);
	Coord3D c;
	c.x = 0.5f * (r.lo.x + r.hi.x);
	c.y = 0.5f * (r.lo.y + r.hi.y);
	c.z = TheTerrainLogic->getGroundHeight(c.x, c.y);
	return c;
}

Bool Runner::centreOf(const std::vector<ObjectID> &ids, Coord3D &c) const
{
	c.zero();
	Int n = 0;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		const Object *o = TheGameLogic->findObjectByID(ids[i]);
		if (o == nullptr)
			continue;
		c.x += o->getPosition()->x;
		c.y += o->getPosition()->y;
		++n;
	}
	if (n == 0)
		return FALSE;
	c.x /= (Real)n;
	c.y /= (Real)n;
	c.z = TheTerrainLogic->getGroundHeight(c.x, c.y);
	return TRUE;
}

// "name", "name[2]", "cc:1", "sel", joined with '+'.
Bool Runner::resolveObjects(const std::string &ref, std::vector<ObjectID> &ids, std::string &error) const
{
	ids.clear();
	std::vector<std::string> parts = split(ref, '+');
	for (size_t p = 0; p < parts.size(); ++p)
	{
		std::string item = parts[p];
		if (item == "sel")
		{
			ids.insert(ids.end(), m_selection.begin(), m_selection.end());
			continue;
		}
		if (item.compare(0, 3, "cc:") == 0)
		{
			Int slot = -1;
			Player *player = parseSigned(item.substr(3), slot) ? ThePlayerList->getPlayerFromSlotIndex(slot) : nullptr;
			Object *found = nullptr;
			for (Object *obj = TheGameLogic->getFirstObject(); obj && player && !found; obj = obj->getNextObject())
				if (obj->getControllingPlayer() == player && obj->isKindOf(KINDOF_COMMANDCENTER) && !obj->isEffectivelyDead())
					found = obj;
			if (found == nullptr)
			{
				error = "no command center for '" + item + "'";
				return FALSE;
			}
			ids.push_back(found->getID());
			continue;
		}
		Int index = -1;
		const size_t br = item.find('[');
		if (br != std::string::npos)
		{
			const size_t close = item.find(']', br);
			if (close == std::string::npos || !parseSigned(item.substr(br + 1, close - br - 1), index) || index < 0)
			{
				error = "bad index in '" + item + "'";
				return FALSE;
			}
			item = item.substr(0, br);
		}
		std::map<std::string, std::vector<ObjectID> >::const_iterator it = m_names.find(item);
		if (it == m_names.end())
		{
			error = "unknown name '" + item + "'";
			return FALSE;
		}
		if (index >= 0)
		{
			if ((size_t)index >= it->second.size())
			{
				error = format("'%s' has only %d objects", item.c_str(), (int)it->second.size());
				return FALSE;
			}
			ids.push_back(it->second[index]);
		}
		else
			ids.insert(ids.end(), it->second.begin(), it->second.end());
	}
	return TRUE;
}

Bool Runner::resolveLive(const std::string &ref, std::vector<Object *> &objs, std::string &error) const
{
	std::vector<ObjectID> ids;
	objs.clear();
	if (!resolveObjects(ref, ids, error))
		return FALSE;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *o = TheGameLogic->findObjectByID(ids[i]);
		if (o && !o->isEffectivelyDead())
			objs.push_back(o);
	}
	if (objs.empty())
	{
		error = "no live objects in '" + ref + "'";
		return FALSE;
	}
	return TRUE;
}

// "x,y", "map", "<objects>", each optionally followed by "+dx,dy", "-dx,dy" or "^forward,left".
Bool Runner::resolvePos(const std::string &spec, Coord3D &pos, std::string &error) const
{
	Real ax = 0, ay = 0;
	{
		const size_t comma = spec.find(',');
		if (comma != std::string::npos && parseReal(spec.substr(0, comma), ax) && parseReal(spec.substr(comma + 1), ay))
		{
			pos.x = ax;
			pos.y = ay;
			pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
			return TRUE;
		}
	}
	size_t cut = std::string::npos;
	for (size_t i = 1; i < spec.size(); ++i)
	{
		const char c = spec[i];
		const char n = i + 1 < spec.size() ? spec[i + 1] : 0;
		if (c == '^' || ((c == '+' || c == '-') && ((n >= '0' && n <= '9') || n == '.')))
		{
			cut = i;
			break;
		}
	}
	const std::string anchor = spec.substr(0, cut);
	Coord3D base;
	if (anchor == "map")
		base = mapCentre();
	else
	{
		std::vector<ObjectID> ids;
		if (!resolveObjects(anchor, ids, error))
			return FALSE;
		if (!centreOf(ids, base))
		{
			error = "no objects left in '" + anchor + "'";
			return FALSE;
		}
	}
	pos = base;
	if (cut != std::string::npos)
	{
		const std::string off = spec.substr(spec[cut] == '-' ? cut : cut + 1);
		const size_t comma = off.find(',');
		Real a = 0, b = 0;
		if (comma == std::string::npos || !parseReal(off.substr(0, comma), a) || !parseReal(off.substr(comma + 1), b))
		{
			error = "bad offset in position '" + spec + "'";
			return FALSE;
		}
		if (spec[cut] == '^')
		{
			const Coord3D c = mapCentre();
			Real fx = c.x - base.x, fy = c.y - base.y;
			const Real len = sqrtf(fx * fx + fy * fy);
			if (len > 1.0f)
				fx /= len, fy /= len;
			else
				fx = 1.0f, fy = 0.0f;
			pos.x = base.x + a * fx - b * fy;
			pos.y = base.y + a * fy + b * fx;
		}
		else
		{
			pos.x = base.x + a;
			pos.y = base.y + b;
		}
	}
	pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
	return TRUE;
}

// The heading and the destination of a move of the selection, worked out the way the order is defined: a click faces the
// way from the group to the destination (or the way the first unit faces, for a short move); a dragged front line faces
// away from the group, with its centre as the destination.
void Runner::recordOrder(const std::vector<ObjectID> &ids, const Coord3D &dest, Bool dragged, const Coord3D &a, const Coord3D &b)
{
	Coord3D centre;
	centre.zero();
	Int n = 0;
	ObjectID firstId = INVALID_ID;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		const Object *o = TheGameLogic->findObjectByID(ids[i]);
		if (o == nullptr || o->getAIUpdateInterface() == nullptr || o->isDisabledByType(DISABLED_HELD))
			continue;
		centre.x += o->getPosition()->x;
		centre.y += o->getPosition()->y;
		++n;
		if (!o->isKindOf(KINDOF_IMMOBILE) && (firstId == INVALID_ID || ids[i] < firstId))
			firstId = ids[i];
	}
	if (n == 0)
		return;
	centre.x /= (Real)n;
	centre.y /= (Real)n;

	Order order;
	order.frame = TheGameLogic->getFrame();
	order.dest = dest;
	if (dragged)
	{
		const Real dx = b.x - a.x, dy = b.y - a.y;
		const Real len = sqrtf(dx * dx + dy * dy);
		Real nx = -dy / len, ny = dx / len;
		if (nx * (dest.x - centre.x) + ny * (dest.y - centre.y) < 0.0f)
			nx = -nx, ny = -ny;
		order.heading = atan2f(ny, nx);
		order.width = len;
	}
	else
	{
		const Real dx = dest.x - centre.x, dy = dest.y - centre.y;
		const Object *first = TheGameLogic->findObjectByID(firstId);
		if (dx * dx + dy * dy > 25.0f * 25.0f || first == nullptr)
			order.heading = atan2f(dy, dx);
		else
			order.heading = first->getOrientation();
	}
	for (size_t i = 0; i < ids.size(); ++i)
		m_orders[ids[i]] = order;
}

void Runner::addCheck(Int index, const Step &s, const char *kind, Bool pass, const Eval &e)
{
	Check c;
	c.step = index + 1;
	c.frame = TheGameLogic->getFrame() - m_startFrame;
	c.kind = kind;
	c.text = s.text;
	c.pass = pass;
	c.detail = e.detail;
	c.measured = e.measured;
	m_checks.push_back(c);
	std::string nums;
	for (size_t i = 0; i < c.measured.size(); ++i)
		nums += format(" %s=%.1f", c.measured[i].first.c_str(), c.measured[i].second);
	printf("ASSISTMATCH_CHECK %s f=%u step %d: %s :%s %s\n", pass ? "PASS" : "FAIL", c.frame, c.step, s.text.c_str(), nums.c_str(), c.detail.c_str());
	fflush(stdout);
}

// ------------------------------------------------------------------------------------------------
// Conditions
// ------------------------------------------------------------------------------------------------
Eval Runner::evalShape(const std::vector<ObjectID> &ids, Int type, const Step &s)
{
	Eval e;
	std::vector<Object *> objs;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *o = TheGameLogic->findObjectByID(ids[i]);
		if (o && !o->isEffectivelyDead())
			objs.push_back(o);
	}
	if (objs.size() < 2)
	{
		e.fail("fewer than two live units");
		return e;
	}
	std::map<ObjectID, Order>::const_iterator ord = m_orders.find(objs[0]->getID());
	if (ord == m_orders.end())
	{
		e.fail("the units were given no move order by the script");
		return e;
	}
	const Order order = ord->second;
	for (size_t i = 1; i < objs.size(); ++i)
	{
		std::map<ObjectID, Order>::const_iterator o2 = m_orders.find(objs[i]->getID());
		if (o2 == m_orders.end() || o2->second.frame != order.frame)
		{
			e.fail("the units were not moved by one order");
			return e;
		}
	}

	// the spacing: given, or the smallest distance between two formation offsets (the slot grid), see layoutSlots
	Real spacing = 0.0f;
	if (s.has("spacing"))
		parseReal(s.opt("spacing"), spacing);
	else
	{
		Real best = 1e30f;
		for (size_t i = 0; i < objs.size(); ++i)
		{
			Coord2D a;
			objs[i]->getFormationOffset(&a);
			for (size_t j = i + 1; j < objs.size(); ++j)
			{
				Coord2D b;
				objs[j]->getFormationOffset(&b);
				const Real d = sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
				if (d < best)
					best = d;
			}
		}
		spacing = best / (type == AFORM_LOOSE ? 2.4f : 1.0f);
	}
	e.num("spacing", spacing);
	if (spacing < 1.0f)
	{
		e.fail("the units have no formation slots (all formation offsets are equal)");
		return e;
	}

	std::vector<AssistSlot> slots;
	PlayerAssist::layoutSlots(type, (Int)objs.size(), spacing, spacing, order.width, slots);
	Real maxDepth = 0.0f;
	for (size_t i = 0; i < slots.size(); ++i)
		maxDepth = std::max(maxDepth, slots[i].m_depth);
	// the expected slots, relative to the destination: the front row ahead, centred in depth on the destination
	const Real fx = cosf(order.heading), fy = sinf(order.heading);
	std::vector<Coord2D> want(slots.size());
	for (size_t i = 0; i < slots.size(); ++i)
	{
		const Real ahead = 0.5f * maxDepth - slots[i].m_depth;
		want[i].x = fx * ahead - fy * slots[i].m_x;
		want[i].y = fy * ahead + fx * slots[i].m_x;
	}

	// 1. the shape: the slots the units were given (their formation offsets) are the expected slots, one each
	//    (pairs nearest first; when the shape is right every distance is about zero)
	struct Pair { Real d; size_t u; size_t s; };
	std::vector<Pair> pairs;
	for (size_t u = 0; u < objs.size(); ++u)
	{
		Coord2D off;
		objs[u]->getFormationOffset(&off);
		for (size_t k = 0; k < want.size(); ++k)
		{
			const Real dx = off.x - want[k].x, dy = off.y - want[k].y;
			Pair p = { sqrtf(dx * dx + dy * dy), u, k };
			pairs.push_back(p);
		}
	}
	std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.d < b.d || (a.d == b.d && (a.u < b.u || (a.u == b.u && a.s < b.s))); });
	std::vector<Bool> unitDone(objs.size(), FALSE), slotDone(want.size(), FALSE);
	Real slotError = 0.0f;
	size_t matched = 0;
	for (size_t i = 0; i < pairs.size() && matched < objs.size(); ++i)
	{
		if (unitDone[pairs[i].u] || slotDone[pairs[i].s])
			continue;
		unitDone[pairs[i].u] = slotDone[pairs[i].s] = TRUE;
		slotError = std::max(slotError, pairs[i].d);
		++matched;
	}

	// 2. the places: every unit stands on its slot at the destination
	Real worst = 0.0f, sum = 0.0f;
	for (size_t u = 0; u < objs.size(); ++u)
	{
		Coord2D off;
		objs[u]->getFormationOffset(&off);
		Coord3D slot = order.dest;
		slot.x += off.x;
		slot.y += off.y;
		const Real d = dist2D(*objs[u]->getPosition(), slot);
		worst = std::max(worst, d);
		sum += d;
	}

	Real tol = 30.0f;
	if (s.has("tol"))
		parseReal(s.opt("tol"), tol);
	const Real slotTol = std::max(2.0f, 0.1f * spacing);
	Int rows = 0;
	for (size_t i = 0; i < slots.size(); ++i)
		rows = std::max(rows, slots[i].m_row + 1);
	e.num("units", (double)objs.size());
	e.num("rows", rows);
	e.num("slotError", slotError);
	e.num("maxError", worst);
	e.num("meanError", sum / (Real)objs.size());
	e.num("heading", order.heading * 180.0 / 3.14159265);
	e.num("width", order.width);
	e.ok = slotError <= slotTol && worst <= tol;
	e.detail = format("%s of %d: slots %s the shape (worst %.1f), worst distance of a unit to its slot %.1f (tol %.0f)", kFormationNames[type],
		(int)objs.size(), slotError <= slotTol ? "match" : "do not match", slotError, worst, tol);
	return e;
}

// s.args[first] is the name of the condition.
Eval Runner::evaluate(const Step &s, size_t first)
{
	Eval e;
	const std::string cond = lowered(s.args[first]);
	const std::vector<std::string> a(s.args.begin() + first + 1, s.args.end());
	const Bool any = s.opt("any", "0") == "1";
	std::string error;

	std::vector<ObjectID> ids;
	if (!resolveObjects(a[0], ids, error))
	{
		e.fail(error);
		return e;
	}

	if (cond == "shape")
	{
		const Int type = formationByName(a[1]);
		if (type <= AFORM_NONE || type >= AFORM_KEEP)
		{
			e.fail("shape needs a layout formation: line, column, wedge, box or loose");
			return e;
		}
		return evalShape(ids, type, s);
	}

	if (cond == "sameshape")
	{
		const std::string name = s.opt("as", a[0].c_str());
		std::map<std::string, std::map<ObjectID, Coord2D> >::const_iterator snap = m_snapshots.find(name);
		Coord3D c;
		if (snap == m_snapshots.end() || !centreOf(ids, c))
		{
			e.fail("no snapshot '" + name + "' (or no units)");
			return e;
		}
		Real worst = 0.0f;
		for (size_t i = 0; i < ids.size(); ++i)
		{
			const Object *o = TheGameLogic->findObjectByID(ids[i]);
			std::map<ObjectID, Coord2D>::const_iterator was = snap->second.find(ids[i]);
			if (o == nullptr || was == snap->second.end())
				continue;
			const Real dx = o->getPosition()->x - c.x - was->second.x;
			const Real dy = o->getPosition()->y - c.y - was->second.y;
			worst = std::max(worst, sqrtf(dx * dx + dy * dy));
		}
		Real tol = 30.0f;
		if (s.has("tol"))
			parseReal(s.opt("tol"), tol);
		e.num("maxError", worst);
		e.ok = worst <= tol;
		e.detail = format("worst change of place relative to the centre %.1f (tol %.0f)", worst, tol);
		return e;
	}

	if (cond == "ahead")
	{
		std::vector<ObjectID> other;
		if (!resolveObjects(a[1], other, error))
		{
			e.fail(error);
			return e;
		}
		std::map<ObjectID, Order>::const_iterator ord = ids.empty() ? m_orders.end() : m_orders.find(ids[0]);
		if (ord == m_orders.end())
		{
			e.fail("the units were given no move order by the script");
			return e;
		}
		const Real fx = cosf(ord->second.heading), fy = sinf(ord->second.heading);
		Real depth[2] = { 0, 0 };
		for (Int g = 0; g < 2; ++g)
		{
			const std::vector<ObjectID> &list = g == 0 ? ids : other;
			Int n = 0;
			for (size_t i = 0; i < list.size(); ++i)
			{
				const Object *o = TheGameLogic->findObjectByID(list[i]);
				if (o == nullptr)
					continue;
				depth[g] += (o->getPosition()->x - ord->second.dest.x) * fx + (o->getPosition()->y - ord->second.dest.y) * fy;
				++n;
			}
			if (n == 0)
			{
				e.fail("no live units");
				return e;
			}
			depth[g] /= (Real)n;
		}
		Real by = 1.0f;
		if (s.has("by"))
			parseReal(s.opt("by"), by);
		e.num("forwardA", depth[0]);
		e.num("forwardB", depth[1]);
		e.ok = depth[0] - depth[1] >= by;
		e.detail = format("%s is %.1f ahead of %s (needs %.0f)", a[0].c_str(), depth[0] - depth[1], a[1].c_str(), by);
		return e;
	}

	if (cond == "homeat")
	{
		Coord3D want, homes;
		if (!resolvePos(a[1], want, error))
		{
			e.fail(error);
			return e;
		}
		homes.zero();
		Int n = 0;
		for (size_t i = 0; i < ids.size(); ++i)
		{
			const ProtectLink *link = ThePlayerAssist->protect().find(ids[i]);
			if (link == nullptr)
				continue;
			homes.x += link->m_home.x;
			homes.y += link->m_home.y;
			++n;
		}
		if (n == 0)
		{
			e.fail("none of the units has a protect link");
			return e;
		}
		homes.x /= (Real)n;
		homes.y /= (Real)n;
		Real tol = 60.0f;
		if (s.has("tol"))
			parseReal(s.opt("tol"), tol);
		const Real d = dist2D(homes, want);
		e.num("distance", d);
		e.num("homeX", homes.x);
		e.num("homeY", homes.y);
		e.ok = d <= tol;
		e.detail = format("the centre of the homes is %.1f from %s (tol %.0f)", d, a[1].c_str(), tol);
		return e;
	}

	if (cond == "near" && s.opt("centre", s.opt("center", "0").c_str()) == "1")
	{
		Coord3D want, c;
		if (!resolvePos(a[1], want, error))
		{
			e.fail(error);
			return e;
		}
		if (!centreOf(ids, c))
		{
			e.fail("no live units");
			return e;
		}
		Real tol = 50.0f;
		if (s.has("tol"))
			parseReal(s.opt("tol"), tol);
		const Real d = dist2D(c, want);
		e.num("distance", d);
		e.ok = d <= tol;
		e.detail = format("the centre is %.1f from %s (tol %.0f)", d, a[1].c_str(), tol);
		return e;
	}

	if (cond == "apart")
	{
		Real closest = 1e30f;
		Int n = 0;
		for (size_t i = 0; i < ids.size(); ++i)
		{
			const Object *o = TheGameLogic->findObjectByID(ids[i]);
			if (o == nullptr || o->isEffectivelyDead())
				continue;
			++n;
			for (size_t j = i + 1; j < ids.size(); ++j)
			{
				const Object *p = TheGameLogic->findObjectByID(ids[j]);
				if (p && !p->isEffectivelyDead())
					closest = std::min(closest, dist2D(*o->getPosition(), *p->getPosition()));
			}
		}
		if (n < 2)
		{
			e.fail("fewer than two live units");
			return e;
		}
		Real min = 0.0f;
		parseReal(s.opt("min", "0"), min);
		e.num("closest", closest);
		e.ok = closest >= min;
		e.detail = format("the closest two units are %.1f apart (needs %.0f)", closest, min);
		return e;
	}

	// per unit conditions
	Coord3D p1, p2;
	std::vector<ObjectID> targets;
	Int formation = -1, state = -1;
	Real tol = 0.0f;
	Real percent = 0.0f;
	const std::map<ObjectID, Coord3D> *places = nullptr;
	if (cond == "atsnapshot")
	{
		std::map<std::string, std::map<ObjectID, Coord3D> >::const_iterator it = m_places.find(s.opt("as", a[0].c_str()));
		if (it == m_places.end())
		{
			e.fail("no snapshot '" + s.opt("as", a[0].c_str()) + "'");
			return e;
		}
		places = &it->second;
		tol = 30.0f;
	}
	else if (cond == "health")
	{
		if ((lowered(a[1]) != "above" && lowered(a[1]) != "below") || !parseReal(a[2], percent))
		{
			e.fail("health <objects> above|below <percent>");
			return e;
		}
	}
	else if (cond == "near" || cond == "nearline")
	{
		if (!resolvePos(a[1], p1, error) || (cond == "nearline" && !resolvePos(a[2], p2, error)))
		{
			e.fail(error);
			return e;
		}
		tol = cond == "near" ? 50.0f : 30.0f;
	}
	else if (cond == "athome")
		tol = ProtectManager::params().m_homeTolerance;
	else if (cond == "formation")
	{
		formation = formationByName(a[1]);
		if (formation < 0)
		{
			e.fail("unknown formation '" + a[1] + "'");
			return e;
		}
	}
	else if (cond == "state")
	{
		for (Int k = 0; k < (Int)ARRAY_SIZE(kStateNames); ++k)
			if (lowered(a[1]) == kStateNames[k])
				state = k;
		if (state < 0)
		{
			e.fail("unknown state '" + a[1] + "' (home, responding, returning)");
			return e;
		}
	}
	else if (cond == "protects" && !resolveObjects(a[1], targets, error))
	{
		e.fail(error);
		return e;
	}
	if (s.has("tol"))
		parseReal(s.opt("tol"), tol);

	Int good = 0, total = 0;
	Real worst = 0.0f;
	std::string states;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *o = TheGameLogic->findObjectByID(ids[i]);
		const Bool live = o != nullptr && !o->isEffectivelyDead();
		++total;
		Bool pass = FALSE;
		const ProtectLink *link = ThePlayerAssist->protect().find(ids[i]);
		if (cond == "dead")
			pass = !live;
		else if (!live)
			pass = FALSE;
		else if (cond == "alive")
			pass = TRUE;
		else if (cond == "formation")
			pass = ThePlayerAssist->formationOf(ids[i]) == formation;
		else if (cond == "linked")
			pass = link != nullptr;
		else if (cond == "unlinked")
			pass = link == nullptr;
		else if (cond == "protects")
		{
			std::vector<ObjectID> covered;
			if (link)
				ThePlayerAssist->protect().resolveTargets(*link, covered);
			pass = link != nullptr;
			for (size_t t = 0; t < targets.size() && pass; ++t)
				pass = std::binary_search(covered.begin(), covered.end(), targets[t]);
		}
		else if (cond == "state")
		{
			pass = link != nullptr && link->m_state == state;
			if (link)
				states += format("%s%d:%s", states.empty() ? "" : " ", (int)ids[i], kStateNames[link->m_state < 3 ? link->m_state : 0]);
		}
		else if (cond == "athome")
		{
			const Real d = link ? dist2D(*o->getPosition(), link->m_home) : 1e9f;
			if (link)
				worst = std::max(worst, d);
			pass = link != nullptr && link->m_state == PROTECT_AT_HOME && d <= tol;
		}
		else if (cond == "near")
		{
			const Real d = dist2D(*o->getPosition(), p1);
			worst = std::max(worst, d);
			pass = d <= tol;
		}
		else if (cond == "nearline")
		{
			const Real vx = p2.x - p1.x, vy = p2.y - p1.y;
			const Real len2 = vx * vx + vy * vy;
			Real t = len2 > 0.0f ? ((o->getPosition()->x - p1.x) * vx + (o->getPosition()->y - p1.y) * vy) / len2 : 0.0f;
			t = std::max(0.0f, std::min(1.0f, t));
			Coord3D q;
			q.x = p1.x + t * vx;
			q.y = p1.y + t * vy;
			q.z = 0.0f;
			const Real d = dist2D(*o->getPosition(), q);
			worst = std::max(worst, d);
			pass = d <= tol;
		}
		else if (cond == "atsnapshot")
		{
			std::map<ObjectID, Coord3D>::const_iterator was = places->find(ids[i]);
			const Real d = was == places->end() ? 1e9f : dist2D(*o->getPosition(), was->second);
			worst = std::max(worst, d);
			pass = d <= tol;
		}
		else if (cond == "health")
		{
			const BodyModuleInterface *body = o->getBodyModule();
			const Real h = body && body->getMaxHealth() > 0.0f ? 100.0f * body->getHealth() / body->getMaxHealth() : 0.0f;
			worst = std::max(worst, h);
			pass = lowered(a[1]) == "above" ? h > percent : h < percent;
		}
		else if (cond == "damaged")
			pass = o->getBodyModule() && o->getBodyModule()->getHealth() < o->getBodyModule()->getMaxHealth();
		else if (cond == "idle")
			pass = o->getAIUpdateInterface() && o->getAIUpdateInterface()->isIdle();
		if (pass)
			++good;
	}
	e.num("units", total);
	e.num("matching", good);
	if (cond == "near" || cond == "nearline" || cond == "athome" || cond == "atsnapshot")
		e.num("maxDistance", worst);
	if (cond == "health")
		e.num("maxHealthPercent", worst);
	e.ok = total > 0 && (any ? good > 0 : good == total);
	e.detail = format("%d of %d units%s", good, total, any ? " (any)" : "");
	if (!states.empty())
		e.detail += " [" + states + "]";
	return e;
}

// ------------------------------------------------------------------------------------------------
// Steps
// ------------------------------------------------------------------------------------------------
void Runner::runStep(Int index, const Step &s, std::string &fatal)
{
	const std::string &verb = s.args[0];
	const std::vector<std::string> a(s.args.begin() + 1, s.args.end());
	std::string error;
	printf("ASSISTMATCH_STEP f=%u step %d: %s\n", TheGameLogic->getFrame() - m_startFrame, index + 1, s.text.c_str());

	if (verb == "wait")
	{
		UnsignedInt n = 0;
		if (!parseUnsigned(a[0], n))
			error = "bad frame count '" + a[0] + "'";
		else
			advance(n, fatal);
	}
	else if (verb == "until" || verb == "expect")
	{
		const Bool negate = lowered(a[0]) == "not";
		const size_t first = negate ? 2 : 1;
		if (verb == "expect")
		{
			Eval e = evaluate(s, first);
			addCheck(index, s, "expect", !e.error && (negate ? !e.ok : e.ok), e);
		}
		else
		{
			UnsignedInt max = kDefaultUntilFrames;
			if (s.has("max") && !parseUnsigned(s.opt("max"), max))
				max = kDefaultUntilFrames;
			const UnsignedInt start = TheGameLogic->getFrame();
			for (;;)
			{
				Eval e = evaluate(s, first);
				const Bool ok = !e.error && (negate ? !e.ok : e.ok);
				const UnsignedInt waited = TheGameLogic->getFrame() - start;
				if (ok || e.error || waited >= max)
				{
					e.num("waited", waited);
					if (!ok && !e.error)
						e.detail = format("not within %u frames: ", max) + e.detail;
					addCheck(index, s, "until", ok, e);
					break;
				}
				if (!advance(1, fatal))
					break;
			}
		}
	}
	else if (verb == "spawn")
	{
		Coord3D at;
		const Bool hasAt = s.has("at");
		if (hasAt && !resolvePos(s.opt("at"), at, error))
			;
		else
		{
			std::vector<ObjectID> made;
			std::vector<std::string> entries = split(a[1], '+');
			for (size_t i = 0; i < entries.size(); ++i)
			{
				int count = 0;
				sscanf(entries[i].c_str(), "%*d:%*[^:]:%d", &count);
				if (PlayerAssist::createTestUnits(entries[i].c_str(), hasAt ? &at : nullptr, &made) < count)
					error = "could not create '" + entries[i] + "'";
				at.y -= 30.0f * (Real)((count + 7) / 8);	// the next entry below the rows of this one
			}
			m_names[a[0]].insert(m_names[a[0]].end(), made.begin(), made.end());
		}
	}
	else if (verb == "name")
	{
		std::vector<ObjectID> ids;
		if (resolveObjects(a[1], ids, error))
			m_names[a[0]] = ids;
	}
	else if (verb == "snapshot")
	{
		std::vector<ObjectID> ids;
		Coord3D c;
		if (resolveObjects(a[0], ids, error) && centreOf(ids, c))
		{
			std::map<ObjectID, Coord2D> &snap = m_snapshots[s.opt("as", a[0].c_str())];
			std::map<ObjectID, Coord3D> &places = m_places[s.opt("as", a[0].c_str())];
			snap.clear();
			places.clear();
			for (size_t i = 0; i < ids.size(); ++i)
			{
				const Object *o = TheGameLogic->findObjectByID(ids[i]);
				if (o == nullptr)
					continue;
				Coord2D d;
				d.x = o->getPosition()->x - c.x;
				d.y = o->getPosition()->y - c.y;
				snap[ids[i]] = d;
				places[ids[i]] = *o->getPosition();
			}
		}
	}
	else if (verb == "dump")
	{
		std::vector<ObjectID> ids;
		if (resolveObjects(a[0], ids, error))
		{
			for (size_t i = 0; i < ids.size(); ++i)
			{
				const Object *o = TheGameLogic->findObjectByID(ids[i]);
				if (o == nullptr)
				{
					printf("ASSISTMATCH_DUMP id=%d gone\n", (int)ids[i]);
					continue;
				}
				Coord2D off;
				o->getFormationOffset(&off);
				const ProtectLink *link = ThePlayerAssist->protect().find(ids[i]);
				const AIUpdateInterface *ai = o->getAIUpdateInterface();
				printf("ASSISTMATCH_DUMP id=%d %s pos=%.0f,%.0f facing=%.0f health=%.0f idle=%d formation=%s fid=%d offset=%.0f,%.0f link=%s home=%.0f,%.0f\n",
					(int)ids[i], o->getTemplate()->getName().str(), o->getPosition()->x, o->getPosition()->y, o->getOrientation() * 180.0f / 3.14159265f,
					o->getBodyModule() ? o->getBodyModule()->getHealth() : 0.0f, ai && ai->isIdle() ? 1 : 0, kFormationNames[ThePlayerAssist->formationOf(ids[i])],
					(int)o->getFormationID(), off.x, off.y, link ? kStateNames[link->m_state < 3 ? link->m_state : 0] : "-",
					link ? link->m_home.x : 0.0f, link ? link->m_home.y : 0.0f);
			}
		}
	}
	else if (verb == "select")
	{
		std::vector<Object *> objs;
		if (resolveLive(a[0], objs, error))
		{
			GameMessage *msg = message(GameMessage::MSG_CREATE_SELECTED_GROUP);
			msg->appendBooleanArgument(TRUE);
			m_selection.clear();
			for (size_t i = 0; i < objs.size(); ++i)
			{
				msg->appendObjectIDArgument(objs[i]->getID());
				m_selection.push_back(objs[i]->getID());
			}
		}
	}
	else if (verb == "group" || verb == "selectgroup")
	{
		Int n = -1;
		if (!parseSigned(a[0], n) || n < 0 || n > 9)
			error = "hotkey groups are 0 to 9";
		else if (verb == "group")
		{
			GameMessage *msg = message((GameMessage::Type)(GameMessage::MSG_CREATE_TEAM0 + n));
			for (size_t i = 0; i < m_selection.size(); ++i)
				msg->appendObjectIDArgument(m_selection[i]);
			m_hotkeys[n] = m_selection;
		}
		else
		{
			message((GameMessage::Type)(GameMessage::MSG_SELECT_TEAM0 + n));
			m_selection = m_hotkeys[n];
		}
	}
	else if (verb == "formation")
	{
		const Int type = formationByName(a[0]);
		if (type < 0)
			error = "unknown formation '" + a[0] + "'";
		else
			message(GameMessage::MSG_ASSIST_FORMATION)->appendIntegerArgument(type);
	}
	else if (verb == "fmove")
	{
		Coord3D pa, pb;
		Int type = s.has("type") ? formationByName(s.opt("type")) : ThePlayerAssist->sharedFormation(m_selection);
		if (!resolvePos(a[0], pa, error) || (a.size() > 1 && !resolvePos(a[1], pb, error)))
			;
		else if (type <= AFORM_NONE || type >= AFORM_KEEP)
			error = "the selection has no layout formation: the user interface would not send a formation move (give type= or wait a frame after 'formation')";
		else
		{
			if (a.size() < 2)
				pb = pa;
			GameMessage *msg = message(GameMessage::MSG_ASSIST_FORMATION_MOVE);
			msg->appendIntegerArgument(type);
			msg->appendLocationArgument(pa);
			msg->appendLocationArgument(pb);
			msg->appendBooleanArgument(s.opt("attack", "0") == "1");
			const Real dl = dist2D(pa, pb);
			if (dl > 2.0f * PATHFIND_CELL_SIZE_F)
			{
				Coord3D mid;
				mid.x = 0.5f * (pa.x + pb.x);
				mid.y = 0.5f * (pa.y + pb.y);
				mid.z = TheTerrainLogic->getGroundHeight(mid.x, mid.y);
				recordOrder(m_selection, mid, TRUE, pa, pb);
			}
			else
				recordOrder(m_selection, pa, FALSE, pa, pb);
		}
	}
	else if (verb == "move" || verb == "attackmove" || verb == "guard")
	{
		Coord3D p;
		if (resolvePos(a[0], p, error))
		{
			GameMessage *msg = message(verb == "move" ? GameMessage::MSG_DO_MOVETO : verb == "attackmove" ? GameMessage::MSG_DO_ATTACKMOVETO : GameMessage::MSG_DO_GUARD_POSITION);
			msg->appendLocationArgument(p);
			if (verb == "guard")
				msg->appendIntegerArgument(GUARDMODE_NORMAL);
			else
				recordOrder(m_selection, p, FALSE, p, p);
		}
	}
	else if (verb == "attack")
	{
		std::vector<Object *> objs;
		if (resolveLive(a[0], objs, error))
			message(GameMessage::MSG_DO_ATTACK_OBJECT)->appendObjectIDArgument(objs[0]->getID());
	}
	else if (verb == "stop")
		message(GameMessage::MSG_DO_STOP);
	else if (verb == "protect")
	{
		std::vector<Object *> protectors;
		std::vector<ObjectID> targets;
		Int squad = -1;
		if (s.has("group") && (!parseSigned(s.opt("group"), squad) || squad < 0 || squad > 9))
			error = "group= is a hotkey group, 0 to 9";
		else if (resolveLive(a[0], protectors, error) && (a.size() < 2 || resolveObjects(a[1], targets, error)))
		{
			if (squad < 0 && targets.empty())
				error = "protect needs the protected objects or group=<n>";
			else
			{
				// (hotkey group or -1, number of protectors, protectors ..., protected ...), see AssistUIProtect.cpp sendProtect()
				GameMessage *msg = message(GameMessage::MSG_ASSIST_PROTECT);
				msg->appendIntegerArgument(squad);
				msg->appendIntegerArgument((Int)protectors.size());
				for (size_t i = 0; i < protectors.size(); ++i)
					msg->appendObjectIDArgument(protectors[i]->getID());
				for (size_t i = 0; i < targets.size(); ++i)
					msg->appendObjectIDArgument(targets[i]);
			}
		}
	}
	else if (verb == "unprotect")
		message(GameMessage::MSG_ASSIST_UNPROTECT);
	else if (verb == "send")
	{
		// any command of the player by its name, with typed arguments: the orders of assists this script has no verb for
		GameMessage::Type type = GameMessage::MSG_INVALID;
		std::string wanted = lowered(a[0]);
		if (wanted.compare(0, 4, "msg_") != 0)
			wanted = "msg_" + wanted;
		for (Int t = GameMessage::MSG_BEGIN_NETWORK_MESSAGES + 1; t < GameMessage::MSG_BEGIN_DEBUG_NETWORK_MESSAGES; ++t)
			if (lowered(GameMessage::getCommandTypeAsString((GameMessage::Type)t)) == wanted)
				type = (GameMessage::Type)t;
		if (type == GameMessage::MSG_INVALID)
			error = "this build has no command " + a[0];
		std::vector<std::pair<std::string, std::string> > typed;
		for (size_t i = 1; i < a.size() && error.empty(); ++i)
		{
			const size_t colon = a[i].find(':');
			if (colon == std::string::npos)
				error = "argument '" + a[i] + "' is not int:, bool:, real:, pos: or obj:";
			else
				typed.push_back(std::make_pair(lowered(a[i].substr(0, colon)), a[i].substr(colon + 1)));
		}
		// resolve everything first: nothing is sent when an argument is wrong
		std::vector<Coord3D> positions(typed.size());
		std::vector<std::vector<ObjectID> > objects(typed.size());
		for (size_t i = 0; i < typed.size() && error.empty(); ++i)
		{
			Int n = 0;
			Real r = 0;
			const std::string &k = typed[i].first, &v = typed[i].second;
			if ((k == "int" || k == "bool") && !parseSigned(v, n))
				error = "bad number '" + v + "'";
			else if (k == "real" && !parseReal(v, r))
				error = "bad number '" + v + "'";
			else if (k == "pos")
				resolvePos(v, positions[i], error);
			else if (k == "obj")
				resolveObjects(v, objects[i], error);
			else if (k != "int" && k != "bool" && k != "real")
				error = "argument type '" + k + "' is not int, bool, real, pos or obj";
		}
		if (error.empty())
		{
			GameMessage *msg = message(type);
			for (size_t i = 0; i < typed.size(); ++i)
			{
				const std::string &k = typed[i].first, &v = typed[i].second;
				Int n = 0;
				Real r = 0;
				if (k == "int")
					parseSigned(v, n), msg->appendIntegerArgument(n);
				else if (k == "bool")
					parseSigned(v, n), msg->appendBooleanArgument(n != 0);
				else if (k == "real")
					parseReal(v, r), msg->appendRealArgument(r);
				else if (k == "pos")
					msg->appendLocationArgument(positions[i]);
				else
					for (size_t o = 0; o < objects[i].size(); ++o)
						msg->appendObjectIDArgument(objects[i][o]);
			}
		}
	}
	else if (verb == "ai")
	{
		std::vector<Object *> objs;
		const std::string order = a.size() > 1 ? lowered(a[1]) : std::string();
		Coord3D p;
		std::vector<Object *> victims;
		if (!resolveLive(a[0], objs, error))
			;
		else if ((order == "move" || order == "attackmove" || order == "guard") && (a.size() < 3 || !resolvePos(a[2], p, error)))
		{
			if (error.empty())
				error = "ai ... " + order + " needs a position";
		}
		else if (order == "attack" && (a.size() < 3 || !resolveLive(a[2], victims, error)))
		{
			if (error.empty())
				error = "ai ... attack needs a target";
		}
		else if (order != "move" && order != "attackmove" && order != "guard" && order != "attack" && order != "stop")
			error = "ai orders: attack, move, attackmove, guard, stop";
		else
		{
			for (size_t i = 0; i < objs.size(); ++i)
			{
				AIUpdateInterface *ai = objs[i]->getAIUpdateInterface();
				if (ai == nullptr)
					continue;
				if (order == "move")
					ai->aiMoveToPosition(&p, CMD_FROM_SCRIPT);
				else if (order == "attackmove")
					ai->aiAttackMoveToPosition(&p, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
				else if (order == "guard")
					ai->aiGuardPosition(&p, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
				else if (order == "attack")
					ai->aiAttackObject(victims[0], NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
				else
					ai->aiIdle(CMD_FROM_SCRIPT);
			}
		}
	}
	else if (verb == "damage")
	{
		std::vector<Object *> objs, by;
		Real amount = 0.0f;
		const Bool percent = !a[1].empty() && a[1][a[1].size() - 1] == '%';
		if (!parseReal(percent ? a[1].substr(0, a[1].size() - 1) : a[1], amount))
			error = "bad damage amount '" + a[1] + "'";
		else if (resolveLive(a[0], objs, error) && (!s.has("by") || resolveLive(s.opt("by"), by, error)))
		{
			for (size_t i = 0; i < objs.size(); ++i)
			{
				DamageInfo info;
				info.in.m_damageType = DAMAGE_UNRESISTABLE;	// the amount asked for, whatever the armour
				info.in.m_deathType = DEATH_NORMAL;
				info.in.m_amount = percent && objs[i]->getBodyModule() ? amount * 0.01f * objs[i]->getBodyModule()->getMaxHealth() : amount;
				if (!by.empty())
				{
					info.in.m_sourceID = by[0]->getID();
					info.in.m_sourceTemplate = by[0]->getTemplate();
					info.in.m_sourcePlayerMask = by[0]->getControllingPlayer() ? by[0]->getControllingPlayer()->getPlayerMask() : 0;
				}
				objs[i]->attemptDamage(&info);
			}
		}
	}
	else if (verb == "kill")
	{
		// the ones that are dead already are no error: "kill" makes sure an attacker is gone
		std::vector<ObjectID> ids;
		if (resolveObjects(a[0], ids, error))
			for (size_t i = 0; i < ids.size(); ++i)
			{
				Object *o = TheGameLogic->findObjectByID(ids[i]);
				if (o && !o->isEffectivelyDead())
					o->kill();
			}
	}

	if (!error.empty())
	{
		Eval e;
		e.detail = error;
		addCheck(index, s, "error", FALSE, e);
	}
}

// ------------------------------------------------------------------------------------------------
Bool Runner::play(std::string &error)
{
	const UnsignedInt loadStart = GetTickCount();
	InitRandom((UnsignedInt)m_cfg.seed);
	GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SKIRMISH);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	TheCommandList->appendMessage(msg);

	while (!(TheGameLogic->getGameMode() == GAME_SKIRMISH && TheGameLogic->getFrame() >= 1))
	{
		if (++m_updates > kMaxUpdatesToStart || TheGameEngine->getQuitting())
		{
			error = "the match did not start (see the engine log)";
			return FALSE;
		}
		TheGameLogic->UPDATE();
	}
	m_loadMs = GetTickCount() - loadStart;
	m_startFrame = 0;

	// The human player must be the local player: the orders are sent in its name.
	m_human = ThePlayerList->getPlayerFromSlotIndex(0);
	if (m_human == nullptr || m_human != ThePlayerList->getLocalPlayer())
	{
		error = "the human player of slot 0 is not the local player";
		return FALSE;
	}
	m_assistsAllowed = ThePlayerAssist != nullptr && ThePlayerAssist->allowed();
	if (ThePlayerAssist == nullptr)
	{
		error = "this build has no player assists";
		return FALSE;
	}
	// idle opponents: computer players without their AI
	for (size_t i = 1; i < m_cfg.players.size(); ++i)
	{
		Player *p = ThePlayerList->getPlayerFromSlotIndex((Int)i);
		if (p && m_cfg.players[i].kind == KIND_IDLE)
			p->deletePlayerAI();
	}
	printf("ASSISTMATCH_START map=%s frame=%u assists=%d human=%d\n", m_cfg.mapPath.c_str(), TheGameLogic->getFrame(), m_assistsAllowed ? 1 : 0, (int)m_human->getPlayerIndex());
	fflush(stdout);

	const UnsignedInt wallStart = GetTickCount();
	std::string fatal;
	for (size_t i = 0; i < m_steps.size() && fatal.empty(); ++i)
	{
		runStep((Int)i, m_steps[i], fatal);
		m_stepsDone = (Int)i + 1;
	}
	m_simMs = GetTickCount() - wallStart;
	if (!fatal.empty())
	{
		error = fatal;
		return FALSE;
	}
	return TRUE;
}

std::string Runner::report(UnsignedInt setupMs, const std::string &abortReason)
{
	Int passed = 0, failed = 0;
	for (size_t i = 0; i < m_checks.size(); ++i)
		(m_checks[i].pass ? passed : failed) += 1;
	const UnsignedInt frames = TheGameLogic ? TheGameLogic->getFrame() - m_startFrame : 0;

	JsonWriter w;
	w.beginObject();
	w.field("schema", kSchema);
	w.field("label", m_cfg.label);
	w.field("map", m_cfg.mapPath.empty() ? m_cfg.map : m_cfg.mapPath);
	w.field("seed", m_cfg.seed);
	w.field("assistsAllowed", (bool)m_assistsAllowed);
	w.key("players");
	w.beginArray();
	for (size_t i = 0; i < m_cfg.players.size(); ++i)
	{
		w.beginObject();
		w.field("slot", (Int)i);
		w.field("kind", m_cfg.players[i].kindName);
		w.field("side", m_cfg.players[i].side);
		w.endObject();
	}
	w.endArray();
	w.key("result");
	w.beginObject();
	const char *outcome = !abortReason.empty() ? "error" : (failed == 0 && passed > 0 ? "pass" : "fail");
	w.field("outcome", outcome);
	if (!abortReason.empty())
		w.field("endReason", abortReason);
	w.field("passed", passed);
	w.field("failed", failed);
	w.field("steps", (Int)m_steps.size());
	w.field("stepsDone", m_stepsDone);
	w.field("frames", frames);
	w.endObject();
	w.key("checks");
	w.beginArray();
	for (size_t i = 0; i < m_checks.size(); ++i)
	{
		const Check &c = m_checks[i];
		w.beginObject();
		w.field("step", c.step);
		w.field("frame", c.frame);
		w.field("kind", c.kind);
		w.field("text", c.text);
		w.field("pass", (bool)c.pass);
		w.field("detail", c.detail);
		w.key("measured");
		w.beginObject();
		for (size_t k = 0; k < c.measured.size(); ++k)
			w.field(c.measured[k].first.c_str(), c.measured[k].second);
		w.endObject();
		w.endObject();
	}
	w.endArray();
	w.key("perf");
	w.beginObject();
	w.field("setupMs", setupMs);
	w.field("loadMs", m_loadMs);
	w.field("simMs", m_simMs);
	w.field("logicFps", m_simMs > 0 ? (double)frames * 1000.0 / (double)m_simMs : 0.0);
	w.endObject();
	w.endObject();
	return w.text();
}

} // namespace

// ================================================================================================
Bool AssistMatch::isRequested()
{
	for (int i = 1; i < __argc; ++i)
	{
		if (stricmp(__argv[i], "-assistMatch") == 0)
			return TRUE;
	}
	return FALSE;
}

Int AssistMatch::run()
{
	const UnsignedInt t0 = GetTickCount();
	setBenchActive(TRUE);

	Config cfg;
	std::vector<Step> steps;
	std::string error;
	if (!parseConfig(cfg, error) || !parseScript(cfg.steps, steps, error))
	{
		printf("ASSISTMATCH_ERROR %s\n", error.c_str());
		Runner runner(cfg, steps);
		printf("ASSISTMATCH_RESULT %s\n", runner.report(0, error).c_str());
		fflush(stdout);
		setBenchActive(FALSE);
		return 1;
	}
	TheAssistOptions.m_debug = TheAssistOptions.m_debug || cfg.debug;

	Runner runner(cfg, steps);
	UnsignedInt setupMs = 0;
	Bool ok = FALSE;
	try
	{
		ok = runner.setup(error);
		setupMs = GetTickCount() - t0;
		if (ok)
			ok = runner.play(error);
	}
	catch (...)
	{
		ok = FALSE;
		error = "an exception ended the match";
	}
	if (!ok)
		printf("ASSISTMATCH_ERROR %s\n", error.c_str());
	const std::string json = runner.report(setupMs, ok ? std::string() : error);
	printf("ASSISTMATCH_RESULT %s\n", json.c_str());
	fflush(stdout);

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData(FALSE);
	setBenchActive(FALSE);
	return ok && json.find("\"outcome\":\"pass\"") != std::string::npos ? 0 : 1;
}
